import javax.swing.*;
import java.awt.*;
import java.time.LocalDate;

public class InputDialog extends JDialog {
    private final JTextField dayField = new JTextField(4);
    private final JTextField monthField = new JTextField(4);
    private final JTextField yearField = new JTextField(6);
    private final JButton okButton = new JButton("OK");
    private final JButton cancelButton = new JButton("Отмена");

    public InputDialog(JFrame owner, LocalDate lastDate) {
        super(owner, "Ввод даты", true);
        setLayout(new GridLayout(4, 2, 5, 5));

        add(new JLabel("День:"));   add(dayField);
        add(new JLabel("Месяц:"));  add(monthField);
        add(new JLabel("Год:"));    add(yearField);
        add(okButton);              add(cancelButton);

        if (lastDate != null) {
            dayField.setText(String.valueOf(lastDate.getDayOfMonth()));
            monthField.setText(String.valueOf(lastDate.getMonthValue()));
            yearField.setText(String.valueOf(lastDate.getYear()));
        }

        pack();
        setLocationRelativeTo(owner);
    }

    public JButton getOkButton()     { return okButton; }
    public JButton getCancelButton() { return cancelButton; }

    public LocalDate parseDate() {
        int d = Integer.parseInt(dayField.getText().trim());
        int m = Integer.parseInt(monthField.getText().trim());
        int y = Integer.parseInt(yearField.getText().trim());
        return LocalDate.of(y, m, d);
    }
}
