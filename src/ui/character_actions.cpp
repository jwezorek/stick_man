#include "character_actions.hpp"

void ui::character_actions::make(canvas::scene& canvas, mdl::project& project) {
    auto candidates = canvas.loose_selection();
    if (!candidates.empty()) {
        auto result = project.make_character(candidates);
        if (!result) QMessageBox::warning(nullptr, "Make Character", "Cannot make a character from this selection.");
    }
}

void ui::character_actions::adopt(canvas::scene& canvas, mdl::project& project, QWidget* parent) {
    auto candidates = canvas.loose_selection();
    if (candidates.empty()) return;
    std::vector<sm::object_id> ids;
    QDialog dialog(parent);
    dialog.setWindowTitle("Add to Character");
    QVBoxLayout layout(&dialog);
    QLabel label("Choose the character to receive the selected skeletons:");
    QComboBox characters;
    for (auto character : project.core().characters()) {
        ids.push_back(character->id());
        characters.addItem(QString::fromStdString(character->name()));
    }
    if (ids.empty()) return;
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&label);
    layout.addWidget(&characters);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return;
    auto id = ids.at(characters.currentIndex());
    if (project.adopt_skeletons(id, candidates) == sm::result::success)
        emit project.select_character(id);
}
