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
