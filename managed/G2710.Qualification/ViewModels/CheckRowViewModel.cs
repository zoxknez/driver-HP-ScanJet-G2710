using System.Windows.Input;
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
    public string Name => _check.Name;
    public string Question => _check.Question;
    public CheckOutcome Outcome => _check.Outcome;

    /// <summary>Ono sto se prikazuje u redu - detalj, a za pitanja samo pitanje.</summary>
    public string Detail =>
        string.IsNullOrWhiteSpace(_check.Detail) ? _check.Question : _check.Detail;

    /// <summary>Kratka rec u znacki.</summary>
    public string Badge => Outcome switch
    {
        CheckOutcome.Pass => "PROSAO",
        CheckOutcome.Fail => "PAO",
        CheckOutcome.Blocked => "PRESKOCEN",
        CheckOutcome.Pending => "CEKA",
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
        CheckOutcome.Blocked => "Nivo bezbednosti ovog paketa ne dozvoljava ovu proveru.",
        CheckOutcome.Pending => "Deo drajvera za ovo jos nije napisan.",
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
        UserAnswer.Yes => "DA",
        UserAnswer.No => "NE",
        _ => "PITANJE",
    };
}
