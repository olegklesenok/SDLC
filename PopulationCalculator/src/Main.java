public class Main {
    public static void main(String[] args) {
        PopulationModel model = new PopulationModel();
        MainFrame view = new MainFrame(model);
        new PopulationController(model, view);
        view.setVisible(true);
    }
}