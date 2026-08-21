namespace G2710.Qualification.ViewModels;

/// <summary>
/// Korak wizarda.
/// </summary>
/// <remarks>
/// Cetiri koraka, i svaki ima svoj ekran. Nema "razno" koraka - cim se pojavi,
/// u njemu zavrsi sve sto niko nije hteo da rasporedi.
/// </remarks>
public enum WizardStep
{
    Welcome = 0,
    Running = 1,
    Questions = 2,
    Results = 3,
}

/// <summary>
/// Stanje u kome se ekran nalazi.
/// </summary>
/// <remarks>
/// Svako od ovih stanja ima svoj prikaz. To je i poenta enum-a: prazan ekran
/// bez objasnjenja je najcesci nacin da alat izgleda pokvareno kada zapravo
/// samo ceka.
/// </remarks>
public enum ScreenState
{
    /// <summary>Jos nista nije pokrenuto.</summary>
    Idle,

    /// <summary>Radi se; traka i ispis su vidljivi.</summary>
    Busy,

    /// <summary>Gotovo, ima sta da se prikaze.</summary>
    Ready,

    /// <summary>Gotovo, ali nema nijednog reda - prazan ishod nije isto sto i greska.</summary>
    Empty,

    /// <summary>Nesto je poslo naopako; poruka objasnjava sta i sta dalje.</summary>
    Failed,
}
