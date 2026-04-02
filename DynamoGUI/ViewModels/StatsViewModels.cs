using System.Collections.ObjectModel;
using System.Globalization;
using Avalonia.Media;

namespace DynamoGUI.ViewModels;

public sealed class StatsEntryViewModel : ViewModelBase
{
    private static readonly IBrush PositiveBrush = new SolidColorBrush(Color.Parse("#22C55E"));
    private static readonly IBrush NegativeBrush = new SolidColorBrush(Color.Parse("#EF4444"));
    private static readonly IBrush NeutralBrush = new SolidColorBrush(Color.Parse("#94A3B8"));

    private long _session;
    private long _total;

    public StatsEntryViewModel(string label)
    {
        Label = label;
    }

    public string Label { get; }

    public long Session
    {
        get => _session;
        private set
        {
            if (SetProperty(ref _session, value))
            {
                OnPropertyChanged(nameof(SessionDisplay));
                OnPropertyChanged(nameof(SessionBrush));
            }
        }
    }

    public long Total
    {
        get => _total;
        private set
        {
            if (SetProperty(ref _total, value))
            {
                OnPropertyChanged(nameof(TotalDisplay));
            }
        }
    }

    public string SessionDisplay =>
        Session > 0
            ? $"+{Session.ToString("N0", CultureInfo.CurrentCulture)}"
            : Session.ToString("N0", CultureInfo.CurrentCulture);

    public string TotalDisplay => Total.ToString("N0", CultureInfo.CurrentCulture);

    public IBrush SessionBrush => Session switch
    {
        > 0 => PositiveBrush,
        < 0 => NegativeBrush,
        _ => NeutralBrush,
    };

    public void Update(long session, long total)
    {
        Session = session;
        Total = total;
    }
}

public sealed class StatsSectionViewModel
{
    public StatsSectionViewModel(string title)
    {
        Title = title;
    }

    public string Title { get; }

    public ObservableCollection<StatsEntryViewModel> Rows { get; } = [];
}

public sealed class ResourceAmountViewModel : ViewModelBase
{
    private long _amount;

    public ResourceAmountViewModel(string label)
    {
        Label = label;
    }

    public string Label { get; }

    public long Amount
    {
        get => _amount;
        private set
        {
            if (SetProperty(ref _amount, value))
                OnPropertyChanged(nameof(AmountDisplay));
        }
    }

    public string AmountDisplay => Amount.ToString("N0", CultureInfo.CurrentCulture);

    public void Update(long amount)
    {
        Amount = amount;
    }
}

public sealed class EnrichmentRowViewModel : ViewModelBase
{
    private string _material = "-";
    private string _display = "-";

    public EnrichmentRowViewModel(string module, string unit)
    {
        Module = module;
        Unit = unit;
    }

    public string Module { get; }
    public string Unit { get; }

    public string Material
    {
        get => _material;
        private set => SetProperty(ref _material, value);
    }

    public string Display
    {
        get => _display;
        private set => SetProperty(ref _display, value);
    }

    public void Update(string material, int amount)
    {
        if (string.IsNullOrEmpty(material) || amount <= 0)
        {
            Material = "-";
            Display = "-";
            return;
        }

        Material = material;
        Display = $"{amount.ToString("N0", CultureInfo.CurrentCulture)} {Unit}";
    }
}
