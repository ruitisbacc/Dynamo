using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Interactivity;
using Avalonia.Media;
using DynamoGUI.ViewModels;
using System;
using System.Linq;

namespace DynamoGUI.Views;

public partial class ProfileEditorView : UserControl
{
    private ProfileEditorViewModel ViewModel => (ProfileEditorViewModel)DataContext!;

    public ProfileEditorView()
    {
        InitializeComponent();
    }

    private async void OnNewClick(object? sender, RoutedEventArgs e)
    {
        var ownerWindow = ResolveOwnerWindow();
        var appBackground = ResolveBrush("AppBgBrush", "#0B1120");
        var dialogBackground = ResolveBrush("CardBgBrush", "#111827");
        var dialogBorder = ResolveBrush("CardBorderBrush", "#1E293B");
        var secondaryText = ResolveBrush("TextSecondaryBrush", "#94A3B8");

        var dialog = new Window
        {
            Title = "New Profile",
            Width = 360,
            Height = 190,
            MinWidth = 360,
            MinHeight = 190,
            WindowStartupLocation = ownerWindow is not null
                ? WindowStartupLocation.CenterOwner
                : WindowStartupLocation.CenterScreen,
            CanResize = false,
            ShowInTaskbar = false,
            Background = appBackground,
        };

        var titleText = new TextBlock
        {
            Text = "Create profile",
            FontSize = 14,
            FontWeight = FontWeight.SemiBold,
            Margin = new Avalonia.Thickness(0, 0, 0, 4),
        };

        var subtitleText = new TextBlock
        {
            Text = "Enter a profile name.",
            FontSize = 12,
            Foreground = secondaryText,
            Margin = new Avalonia.Thickness(0, 0, 0, 14),
        };

        var nameBox = new TextBox
        {
            Watermark = "Profile name",
            FontSize = 13,
            Margin = new Avalonia.Thickness(0, 0, 0, 12),
        };
        nameBox.Classes.Add("input");

        var createBtn = new Button { Content = "Create", FontSize = 12 };
        createBtn.Classes.Add("primary");
        var cancelBtn = new Button { Content = "Cancel", FontSize = 12 };
        cancelBtn.Classes.Add("header-btn");

        var buttonsPanel = new StackPanel
        {
            Orientation = Avalonia.Layout.Orientation.Horizontal,
            Spacing = 8,
            HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
        };
        buttonsPanel.Children.Add(createBtn);
        buttonsPanel.Children.Add(cancelBtn);

        var panel = new StackPanel
        {
            Children = { titleText, subtitleText, nameBox, buttonsPanel },
        };

        var surface = new Border
        {
            Background = dialogBackground,
            BorderBrush = dialogBorder,
            BorderThickness = new Avalonia.Thickness(1),
            CornerRadius = new CornerRadius(10),
            Margin = new Avalonia.Thickness(14),
            Padding = new Avalonia.Thickness(18),
            Child = panel,
        };

        dialog.Content = surface;

        string? result = null;
        createBtn.Click += (_, _) => { result = nameBox.Text; dialog.Close(); };
        cancelBtn.Click += (_, _) => dialog.Close();
        nameBox.KeyDown += (_, args) =>
        {
            if (args.Key == Avalonia.Input.Key.Enter)
            {
                result = nameBox.Text;
                dialog.Close();
            }
        };

        dialog.Opened += (_, _) => nameBox.Focus();

        if (ownerWindow is not null)
        {
            await dialog.ShowDialog(ownerWindow);
        }
        else
        {
            dialog.Show();
            dialog.Activate();
        }

        if (!string.IsNullOrWhiteSpace(result))
            ViewModel.CreateNew(result);
    }

    private async void OnSaveClick(object? sender, RoutedEventArgs e)
    {
        await ViewModel.SaveAsync();
    }

    private async void OnVariantSettingsClick(object? sender, RoutedEventArgs e)
    {
        if (sender is not Button { Tag: NpcVariantRuleViewModel variant })
        {
            return;
        }

        var ownerWindow = ResolveOwnerWindow();
        var appBackground = ResolveBrush("AppBgBrush", "#0B1120");
        var dialogBackground = ResolveBrush("CardBgBrush", "#111827");
        var dialogBorder = ResolveBrush("CardBorderBrush", "#1E293B");
        var secondaryText = ResolveBrush("TextSecondaryBrush", "#94A3B8");
        var dangerText = ResolveBrush("RedBrush", "#EF4444");

        var dialog = new Window
        {
            Title = $"{variant.DialogTitle} Settings",
            Width = 420,
            Height = 280,
            MinWidth = 420,
            MinHeight = 280,
            WindowStartupLocation = ownerWindow is not null
                ? WindowStartupLocation.CenterOwner
                : WindowStartupLocation.CenterScreen,
            CanResize = false,
            ShowInTaskbar = false,
            Background = appBackground,
        };

        var titleText = new TextBlock
        {
            Text = variant.DialogTitle,
            FontSize = 14,
            FontWeight = FontWeight.SemiBold,
            Margin = new Thickness(0, 0, 0, 4),
        };

        var subtitleText = new TextBlock
        {
            Text = "Configure distance and targeting behavior for this NPC variant.",
            FontSize = 12,
            Foreground = secondaryText,
            Margin = new Thickness(0, 0, 0, 14),
        };

        var rangeLabel = new TextBlock
        {
            Text = "Range",
            FontSize = 12,
            Foreground = secondaryText,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
        };

        var rangeBox = new TextBox
        {
            Text = variant.Range.ToString(),
            FontSize = 13,
        };
        rangeBox.Classes.Add("input");

        var rangeHint = new TextBlock
        {
            Text = $"Allowed: {NpcVariantRuleViewModel.MinRange}-{NpcVariantRuleViewModel.MaxRange}",
            FontSize = 11,
            Foreground = secondaryText,
            Margin = new Thickness(120, 4, 0, 0),
        };

        var rangeRow = new Grid
        {
            ColumnDefinitions = new ColumnDefinitions("110,*"),
            ColumnSpacing = 10,
        };
        rangeRow.Children.Add(rangeLabel);
        rangeRow.Children.Add(rangeBox);
        Grid.SetColumn(rangeBox, 1);

        var followLowHpCheck = new CheckBox
        {
            Content = "Follow on low HP",
            IsChecked = variant.FollowOnLowHp,
            FontSize = 12,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
        };

        var followThresholdBox = new TextBox
        {
            Text = variant.FollowOnLowHpPercent.ToString(),
            FontSize = 13,
            IsEnabled = variant.FollowOnLowHp,
            Width = 64,
            Watermark = "%",
        };
        followThresholdBox.Classes.Add("input");

        var followThresholdRow = new StackPanel
        {
            Orientation = Avalonia.Layout.Orientation.Horizontal,
            Spacing = 8,
            Margin = new Thickness(0, 6, 0, 0),
            Children = { followLowHpCheck, followThresholdBox },
        };

        var ignoreOwnershipCheck = new CheckBox
        {
            Content = "Ignore ownership",
            IsChecked = variant.IgnoreOwnership,
            FontSize = 12,
        };

        followLowHpCheck.IsCheckedChanged += (_, _) =>
        {
            followThresholdBox.IsEnabled = followLowHpCheck.IsChecked == true;
        };

        var errorText = new TextBlock
        {
            FontSize = 11,
            Foreground = dangerText,
            IsVisible = false,
        };

        var saveBtn = new Button { Content = "Apply", FontSize = 12 };
        saveBtn.Classes.Add("primary");
        var cancelBtn = new Button { Content = "Cancel", FontSize = 12 };
        cancelBtn.Classes.Add("header-btn");

        var buttonsPanel = new StackPanel
        {
            Orientation = Avalonia.Layout.Orientation.Horizontal,
            Spacing = 8,
            HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Right,
        };
        buttonsPanel.Children.Add(saveBtn);
        buttonsPanel.Children.Add(cancelBtn);

        var panel = new StackPanel
        {
            Spacing = 0,
            Children =
            {
                titleText,
                subtitleText,
                rangeRow,
                rangeHint,
                followThresholdRow,
                ignoreOwnershipCheck,
                errorText,
                buttonsPanel
            },
        };

        var surface = new Border
        {
            Background = dialogBackground,
            BorderBrush = dialogBorder,
            BorderThickness = new Thickness(1),
            CornerRadius = new CornerRadius(10),
            Margin = new Thickness(14),
            Padding = new Thickness(18),
            Child = panel,
        };

        dialog.Content = surface;

        void ApplyAndClose()
        {
            if (!TryParseVariantRange(rangeBox.Text, out var parsedRange))
            {
                errorText.Text = $"Range must be {NpcVariantRuleViewModel.MinRange}-{NpcVariantRuleViewModel.MaxRange}.";
                errorText.IsVisible = true;
                rangeBox.Focus();
                return;
            }

            var followEnabled = followLowHpCheck.IsChecked == true;
            var hasValidLowHpPercent = TryParseLowHpPercent(followThresholdBox.Text, out var followThreshold);
            if (followEnabled && !hasValidLowHpPercent)
            {
                errorText.Text =
                    $"Low HP threshold must be {NpcVariantRuleViewModel.MinLowHpPercent}-{NpcVariantRuleViewModel.MaxLowHpPercent}%.";
                errorText.IsVisible = true;
                followThresholdBox.Focus();
                return;
            }
            if (!hasValidLowHpPercent)
            {
                followThreshold = variant.FollowOnLowHpPercent;
            }

            errorText.IsVisible = false;
            variant.Range = parsedRange;
            variant.FollowOnLowHp = followEnabled;
            variant.FollowOnLowHpPercent = followThreshold;
            variant.IgnoreOwnership = ignoreOwnershipCheck.IsChecked == true;
            dialog.Close();
        }

        saveBtn.Click += (_, _) => ApplyAndClose();
        cancelBtn.Click += (_, _) => dialog.Close();
        rangeBox.KeyDown += (_, args) =>
        {
            if (args.Key == Avalonia.Input.Key.Enter)
            {
                ApplyAndClose();
            }
            else if (args.Key == Avalonia.Input.Key.Escape)
            {
                dialog.Close();
            }
        };

        dialog.Opened += (_, _) =>
        {
            rangeBox.Focus();
            rangeBox.SelectAll();
        };

        if (ownerWindow is not null)
        {
            await dialog.ShowDialog(ownerWindow);
        }
        else
        {
            dialog.Show();
            dialog.Activate();
        }
    }

    private Window? ResolveOwnerWindow()
    {
        if (TopLevel.GetTopLevel(this) is Window window)
        {
            return window;
        }

        if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            return desktop.Windows.LastOrDefault(candidate => candidate.IsActive)
                ?? desktop.MainWindow;
        }

        return null;
    }

    private static IBrush ResolveBrush(string resourceKey, string fallbackColor)
    {
        if (Application.Current?.TryFindResource(resourceKey, out var resource) == true &&
            resource is IBrush brush)
        {
            return brush;
        }

        return new SolidColorBrush(Color.Parse(fallbackColor));
    }

    private static bool TryParseVariantRange(string? text, out int range)
    {
        if (int.TryParse(text?.Trim(), out var parsed))
        {
            range = Math.Clamp(
                parsed,
                NpcVariantRuleViewModel.MinRange,
                NpcVariantRuleViewModel.MaxRange
            );
            return true;
        }

        range = NpcVariantRuleViewModel.MinRange;
        return false;
    }

    private static bool TryParseLowHpPercent(string? text, out int percent)
    {
        if (int.TryParse(text?.Trim(), out var parsed))
        {
            percent = Math.Clamp(
                parsed,
                NpcVariantRuleViewModel.MinLowHpPercent,
                NpcVariantRuleViewModel.MaxLowHpPercent
            );
            return true;
        }

        percent = NpcVariantRuleViewModel.MinLowHpPercent;
        return false;
    }
}
