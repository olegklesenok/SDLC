import javax.swing.*;
import java.awt.*;
import java.time.LocalDate;
import java.time.format.DateTimeFormatter;
import java.util.Observable;
import java.util.Observer;

public class MainFrame extends JFrame implements Observer {
    private final JLabel lastInputLabel = new JLabel("Последний ввод: —");
    private final JLabel resultLabel = new JLabel("Население: —");
    private final JButton btnInput = new JButton("Ввести данные");
    private final PopulationModel model;

    public MainFrame(PopulationModel model) {
        this.model = model;
        setTitle("Калькулятор населения мира");
        setDefaultCloseOperation(EXIT_ON_CLOSE);
        setLayout(new GridLayout(3, 1, 10, 10));
        setSize(450, 200);
        setLocationRelativeTo(null);

        add(lastInputLabel);
        add(resultLabel);
        add(btnInput);

        model.addObserver(this);
    }

    public JButton getBtnInput() {
        return btnInput;
    }

    public void showLastInput(LocalDate date) {
        if (date != null) {
            lastInputLabel.setText("Последний ввод: " +
                    date.format(DateTimeFormatter.ofPattern("dd.MM.yyyy")));
        }
    }

    public void showResult(long population) {
        resultLabel.setText("Население: " + String.format("%,d", population) + " чел.");
    }

    public void showError(String msg) {
        JOptionPane.showMessageDialog(this, msg, "Ошибка", JOptionPane.ERROR_MESSAGE);
    }

    @Override
    public void update(Observable o, Object arg) {
        LocalDate date = (LocalDate) arg;
        showLastInput(date);
        long pop = model.calculatePopulation(date);
        if (pop == -1) {
            showError("Дата вне поддерживаемого диапазона (2010–2026)");
            resultLabel.setText("Население: —");
        } else {
            showResult(pop);
        }
    }
}