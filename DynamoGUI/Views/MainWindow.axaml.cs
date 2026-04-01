using Avalonia.Controls;
using Avalonia.Interactivity;
using DynamoGUI.Services;
using DynamoGUI.ViewModels;
using System;

namespace DynamoGUI.Views;

public partial class MainWindow : Window
{
    private MainViewModel ViewModel => (MainViewModel)DataContext!;

    public MainWindow()
        : this(new AppSession())
    {
    }

    public MainWindow(AppSession session)
    {
        InitializeComponent();
        DataContext = new MainViewModel(session);

        if (MapView is not null)
            MapView.MapClicked += OnMapClicked;

        _ = ViewModel.RefreshAsync();
    }

    private async void OnMapClicked(int gameX, int gameY)
    {
        await ViewModel.MoveToAsync(gameX, gameY);
    }

    private async void OnStartClick(object? sender, RoutedEventArgs e)
    {
        await ViewModel.StartBotAsync();
    }

    private async void OnStopClick(object? sender, RoutedEventArgs e)
    {
        await ViewModel.StopBotAsync();
    }

    private async void OnPauseClick(object? sender, RoutedEventArgs e)
    {
        await ViewModel.PauseBotAsync();
    }

    private async void OnDisconnectClick(object? sender, RoutedEventArgs e)
    {
        await ViewModel.DisconnectAsync();
        var loginWindow = new LoginWindow();
        loginWindow.Show();
        Close();
    }

    private void OnNavigateClick(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button button || button.Tag is not string tag)
            return;

        switch (tag)
        {
            case "Dashboard": ViewModel.ShowDashboard(); break;
            case "Stats": ViewModel.ShowStats(); break;
            case "Profiles": ViewModel.ShowProfiles(); break;
        }
    }

    protected override void OnClosed(EventArgs e)
    {
        base.OnClosed(e);
        if (MapView is not null)
            MapView.MapClicked -= OnMapClicked;
        ViewModel.Dispose();
    }
}
