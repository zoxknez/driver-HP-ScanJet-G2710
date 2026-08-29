using System.Windows;
using G2710.Localization;

namespace G2710.App;

public partial class App : Application
{
    /// <summary>
    /// Program se ne pokrece ako se ne slaze sa svojom bibliotekom.
    /// </summary>
    /// <remarks>
    /// Ovde, a ne u view modelu: dugme "Skeniraj" je aktivno od prve sekunde,
    /// pa bi provera vezana za "Proveri vezu" mogla biti preskocena. Jedino
    /// mesto kroz koje se sigurno prolazi pre svakog native poziva je
    /// pokretanje procesa.
    ///
    /// Prozor koji ostane otvoren a ne moze nista bio bi gori od jasne poruke:
    /// nijedan poziv nece proci dok se fajlovi ne uskladе.
    /// </remarks>
    protected override void OnStartup(StartupEventArgs e)
    {
        string? problem = LibraryCheck.Check();
        if (problem is not null)
        {
            MessageBox.Show(problem, Strings.Get("Product_Name"),
                            MessageBoxButton.OK, MessageBoxImage.Error);
            Shutdown(2);
            return;
        }
        base.OnStartup(e);
    }
}
