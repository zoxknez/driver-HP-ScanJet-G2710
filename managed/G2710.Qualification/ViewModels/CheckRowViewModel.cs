using System.Windows.Input;
using G2710.Localization;
using G2710.Qualification.Models;

namespace G2710.Qualification.ViewModels;

/// <summary>Jedan red u listi rezultata, ili jedno pitanje.</summary>
public sealed class CheckRowViewModel : Observable
{
    private readonly CheckResult _check;

    public CheckRowViewModel(CheckResult check)
    {
        _check = check;
        AnswerYesCommand = new RelayCommand(() => Answer = UserAnswer.Yes);
        AnswerNoCommand = new RelayCommand(() => Answer = UserAnswer.No);
    }

    public CheckResult Model => _check;

    public string Id => _check.Id;

    /// <summary>Naziv provere na jeziku korisnika.</summary>
    /// <remarks>
    /// Izvestaj koji alat pise ostaje na engleskom - njega cita onaj kome se
    /// salje. Prevodi se samo ono sto stoji na ekranu, i to po ID-u provere.
    /// Provera koju prevod jos ne poznaje prikazuje se onako kako ju je alat
    /// nazvao; prazno polje bi bilo gore od engleskog natpisa.
    /// </remarks>
    public string Name => Localized("Wiz_Check_", _check.Name);

    public string Question => Localized("Wiz_Ask_", _check.Question);

    private string Localized(string prefix, string fallback)
    {
        if (string.IsNullOrEmpty(_check.Id))
        {
            return fallback;
        }
        var text = Strings.Get(prefix + _check.Id.Replace('.', '_'));
        return text.StartsWith('[') ? fallback : text;
    }

    public CheckOutcome Outcome => _check.Outcome;

    /// <summary>Ono sto se prikazuje u redu - detalj, a za pitanja samo pitanje.</summary>
    public string Detail =>
        string.IsNullOrWhiteSpace(_check.Detail) ? _check.Question : _check.Detail;

    /// <summary>Kratka rec u znacki.</summary>
    public string Badge => Outcome switch
    {
        CheckOutcome.Pass => Strings.Get("Wiz_Badge_Pass"),
        CheckOutcome.Fail => Strings.Get("Wiz_Badge_Fail"),
        CheckOutcome.Blocked => Strings.Get("Wiz_Badge_Blocked"),
        CheckOutcome.Pending => Strings.Get("Wiz_Badge_Pending"),
        CheckOutcome.Ask => AnsweredBadge(),
        _ => "?",
    };

    /// <summary>Kljuc cetkice u paleti; pogled ga pretvara u boju.</summary>
    public string BadgeTone => Outcome switch
    {
        CheckOutcome.Pass => "Pass",
        CheckOutcome.Fail => "Fail",
        CheckOutcome.Blocked => "Wait",
        CheckOutcome.Pending => "Wait",
        CheckOutcome.Ask => Answer switch
        {
            UserAnswer.Yes => "Pass",
            UserAnswer.No => "Fail",
            _ => "Ask",
        },
        _ => "Wait",
    };

    /// <summary>Zasto provera nije pokrenuta - prikazuje se ispod imena.</summary>
    public string? Explanation => Outcome switch
    {
        CheckOutcome.Blocked => Strings.Get("Wiz_Why_Blocked"),
        CheckOutcome.Pending => Strings.Get("Wiz_Why_Pending"),
        _ => null,
    };

    public bool NeedsAnswer => Outcome == CheckOutcome.Ask;

    public UserAnswer Answer
    {
        get => _check.Answer;
        set
        {
            if (_check.Answer == value)
            {
                return;
            }
            _check.Answer = value;
            Raise();
            Raise(nameof(Badge));
            Raise(nameof(BadgeTone));
            Raise(nameof(IsAnswered));
            Raise(nameof(IsYes));
            Raise(nameof(IsNo));
            AnswerChanged?.Invoke();
        }
    }

    public bool IsAnswered => Answer != UserAnswer.Unanswered;
    public bool IsYes => Answer == UserAnswer.Yes;
    public bool IsNo => Answer == UserAnswer.No;

    public ICommand AnswerYesCommand { get; }
    public ICommand AnswerNoCommand { get; }

    /// <summary>Javlja se kada korisnik odgovori, da wizard osvezi dugme "dalje".</summary>
    public System.Action? AnswerChanged { get; set; }

    private string AnsweredBadge() => Answer switch
    {
        UserAnswer.Yes => Strings.Get("Button_Yes"),
        UserAnswer.No => Strings.Get("Button_No"),
        _ => Strings.Get("Wiz_Badge_Question"),
    };
}
