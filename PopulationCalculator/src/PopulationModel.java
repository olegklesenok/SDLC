import java.time.LocalDate;
import java.time.temporal.ChronoUnit;
import java.util.Observable;
import java.util.TreeMap;

public class PopulationModel extends Observable {
    private final TreeMap<Integer, Long> data = new TreeMap<>();
    private LocalDate currentDate;

    public PopulationModel() {
        data.put(2010, 7_021_732_148L);
        data.put(2015, 7_470_491_872L);
        data.put(2020, 7_887_001_292L);
        data.put(2024, 8_161_972_572L);
        data.put(2025, 8_231_613_070L);
        data.put(2026, 8_300_678_395L);
    }

    public long calculatePopulation(LocalDate date) {
        int y = date.getYear();
        if (y < data.firstKey() || y > data.lastKey()) return -1;

        var lower = data.floorEntry(y);
        var upper = data.ceilingEntry(y);
        if (lower.getKey().equals(upper.getKey())) return lower.getValue();

        LocalDate d1 = LocalDate.of(lower.getKey(), 1, 1);
        LocalDate d2 = LocalDate.of(upper.getKey(), 1, 1);
        long totalDays = ChronoUnit.DAYS.between(d1, d2);
        long passedDays = ChronoUnit.DAYS.between(d1, date);
        double ratio = (double) passedDays / totalDays;
        return Math.round(lower.getValue() + ratio * (upper.getValue() - lower.getValue()));
    }

    public void setDate(LocalDate date) {
        this.currentDate = date;
        setChanged();
        notifyObservers(date);
    }

    public LocalDate getCurrentDate() {
        return currentDate;
    }
}