import javax.swing.*;
import java.time.LocalDate;

public class PopulationController {
    private final PopulationModel model;
    private final MainFrame view;

    public PopulationController(PopulationModel model, MainFrame view) {
        this.model = model;
        this.view = view;
        view.getBtnInput().addActionListener(e -> openInputDialog());
    }

    private void openInputDialog() {
        InputDialog dialog = new InputDialog(view, model.getCurrentDate());

        dialog.getOkButton().addActionListener(ev -> {
            try {
                LocalDate date = dialog.parseDate();
                dialog.dispose();
                model.setDate(date);
            } catch (Exception ex) {
                JOptionPane.showMessageDialog(dialog,
                        "Некорректная дата. Проверьте день, месяц, год.",
                        "Ошибка", JOptionPane.ERROR_MESSAGE);
            }
        });

        dialog.getCancelButton().addActionListener(ev -> dialog.dispose());
        dialog.setVisible(true);
    }
}