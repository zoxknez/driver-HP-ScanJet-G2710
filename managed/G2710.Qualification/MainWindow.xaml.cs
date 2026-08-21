using System.Windows;
using G2710.Qualification.ViewModels;

namespace G2710.Qualification;

public partial class MainWindow : Window
{
    public MainWindow()
    {
        InitializeComponent();

        // Jedan model prikaza za ceo prozor. Ekrani ga nasledjuju kroz
        // DataContext, pa nijedan pogled ne pravi svoj - inace bi dva ekrana
        // gledala u dva razlicita stanja.
        DataContext = new MainViewModel();
    }
}
