using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;

namespace G2710.App;

public partial class MainWindow : Window
{
    private Point? _cropStart;

    public MainWindow()
    {
        InitializeComponent();
        DataContext = new MainViewModel();
    }

    private void PreviewSurface_MouseLeftButtonDown(object sender, MouseButtonEventArgs e)
    {
        if (PreviewControl.Source is null) return;
        _cropStart = e.GetPosition(PreviewSurface);
        PreviewSurface.CaptureMouse();
        CropRectangle.Visibility = Visibility.Visible;
        UpdateCrop(_cropStart.Value, _cropStart.Value);
    }

    private void PreviewSurface_MouseMove(object sender, MouseEventArgs e)
    {
        if (_cropStart is { } start && e.LeftButton == MouseButtonState.Pressed)
            UpdateCrop(start, e.GetPosition(PreviewSurface));
    }

    private void PreviewSurface_MouseLeftButtonUp(object sender, MouseButtonEventArgs e)
    {
        if (_cropStart is { } start)
        {
            UpdateCrop(start, e.GetPosition(PreviewSurface));
            PreviewSurface.ReleaseMouseCapture();
            _cropStart = null;
        }
    }

    private void UpdateCrop(Point start, Point end)
    {
        if (PreviewControl.Source is not BitmapSource bitmap || DataContext is not MainViewModel model) return;
        var scale = System.Math.Min(PreviewSurface.ActualWidth / bitmap.PixelWidth,
                                    PreviewSurface.ActualHeight / bitmap.PixelHeight);
        if (double.IsNaN(scale) || double.IsInfinity(scale) || scale <= 0) return;
        var width = bitmap.PixelWidth * scale;
        var height = bitmap.PixelHeight * scale;
        var leftOffset = (PreviewSurface.ActualWidth - width) / 2;
        var topOffset = (PreviewSurface.ActualHeight - height) / 2;
        var a = new Point(System.Math.Clamp(start.X, leftOffset, leftOffset + width), System.Math.Clamp(start.Y, topOffset, topOffset + height));
        var b = new Point(System.Math.Clamp(end.X, leftOffset, leftOffset + width), System.Math.Clamp(end.Y, topOffset, topOffset + height));
        var left = System.Math.Min(a.X, b.X); var top = System.Math.Min(a.Y, b.Y);
        Canvas.SetLeft(CropRectangle, left); Canvas.SetTop(CropRectangle, top);
        CropRectangle.Width = System.Math.Abs(a.X - b.X); CropRectangle.Height = System.Math.Abs(a.Y - b.Y);
        model.CropLeft = (int)System.Math.Floor((left - leftOffset) / scale);
        model.CropTop = (int)System.Math.Floor((top - topOffset) / scale);
        model.CropWidth = (int)System.Math.Ceiling(CropRectangle.Width / scale);
        model.CropHeight = (int)System.Math.Ceiling(CropRectangle.Height / scale);
    }
}
