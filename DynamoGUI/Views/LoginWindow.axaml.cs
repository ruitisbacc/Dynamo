using Avalonia.Controls;
using DynamoGUI.ViewModels;
using System;

namespace DynamoGUI.Views;

public partial class LoginWindow : Window
{
    private readonly LoginViewModel _vm;
    private bool _handedOff;

    public LoginWindow()
    {
        InitializeComponent();
        _vm = new LoginViewModel();
        DataContext = _vm;
        _vm.LoginSuccessful += OnLoginSuccessful;
    }

    private void OnLoginSuccessful()
    {
        _handedOff = true;
        var main = new MainWindow(_vm.Session!);
        main.Show();
        Close();
    }

    protected override void OnClosed(EventArgs e)
    {
        base.OnClosed(e);
        if (!_handedOff)
        {
            _vm.Session?.Dispose();
        }
    }
}
