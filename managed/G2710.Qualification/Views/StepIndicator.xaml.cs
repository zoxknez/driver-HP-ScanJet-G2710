using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;

namespace G2710.Qualification.Views;

/// <summary>Jedan korak u bocnoj traci.</summary>
public partial class StepIndicator : UserControl
{
    public StepIndicator()
    {
        InitializeComponent();
        Apply();
    }

    public static readonly DependencyProperty NumberProperty = DependencyProperty.Register(
        nameof(Number), typeof(string), typeof(StepIndicator),
        new PropertyMetadata("1", OnAppearanceChanged));

    public static readonly DependencyProperty TitleProperty = DependencyProperty.Register(
        nameof(Title), typeof(string), typeof(StepIndicator), new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty DescriptionProperty = DependencyProperty.Register(
        nameof(Description), typeof(string), typeof(StepIndicator),
        new PropertyMetadata(string.Empty));

    public static readonly DependencyProperty IsActiveProperty = DependencyProperty.Register(
        nameof(IsActive), typeof(bool), typeof(StepIndicator),
        new PropertyMetadata(false, OnAppearanceChanged));

    public static readonly DependencyProperty IsDoneProperty = DependencyProperty.Register(
        nameof(IsDone), typeof(bool), typeof(StepIndicator),
        new PropertyMetadata(false, OnAppearanceChanged));

    public string Number
    {
        get => (string)GetValue(NumberProperty);
        set => SetValue(NumberProperty, value);
    }

    public string Title
    {
        get => (string)GetValue(TitleProperty);
        set => SetValue(TitleProperty, value);
    }

    public string Description
    {
        get => (string)GetValue(DescriptionProperty);
        set => SetValue(DescriptionProperty, value);
    }

    public bool IsActive
    {
        get => (bool)GetValue(IsActiveProperty);
        set => SetValue(IsActiveProperty, value);
    }

    public bool IsDone
    {
        get => (bool)GetValue(IsDoneProperty);
        set => SetValue(IsDoneProperty, value);
    }

    private static void OnAppearanceChanged(DependencyObject source,
                                            DependencyPropertyChangedEventArgs args)
    {
        (source as StepIndicator)?.Apply();
    }

    /// <summary>
    /// Tri stanja, tri izgleda.
    /// </summary>
    /// <remarks>
    /// Radi se u kodu, a ne triggerima, jer stanje zavisi od DVE osobine
    /// odjednom. Triggeri bi za to trazili MultiDataTrigger po kombinaciji, a
    /// to je vise XAML-a nego sto ova odluka vredi.
    /// </remarks>
    private void Apply()
    {
        if (Bubble is null || BubbleText is null || TitleText is null)
        {
            return;
        }

        if (IsActive)
        {
            Bubble.Background = (Brush)FindResource("Accent");
            BubbleText.Foreground = Brushes.White;
            BubbleText.Text = Number;
            TitleText.Foreground = (Brush)FindResource("TextOnDark");
        }
        else if (IsDone)
        {
            Bubble.Background = (Brush)FindResource("Pass");
            BubbleText.Foreground = Brushes.White;

            // Kvacica umesto broja: zavrsen korak se prepoznaje bez citanja.
            BubbleText.Text = "✓";
            TitleText.Foreground = (Brush)FindResource("TextOnDarkMuted");
        }
        else
        {
            Bubble.Background = new SolidColorBrush(Color.FromRgb(0x33, 0x40, 0x4D));
            BubbleText.Foreground = (Brush)FindResource("TextOnDarkMuted");
            BubbleText.Text = Number;
            TitleText.Foreground = (Brush)FindResource("TextOnDarkMuted");
        }
    }
}
