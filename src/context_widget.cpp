#include "aiws/gui/context_widget.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QPlainTextEdit>
#include <QSlider>
#include <QStringList>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>

namespace aiws::gui {

ContextWidget::ContextWidget(QWidget* parent) : QWidget(parent) {
    // TODO: Construct and lay out the Context area, configure the token-budget
    // control, connect local events, and establish initial state.
    auto* bx = new QGroupBox("Context", this);

    // token budget slider
    context_budget_slider_ = new QSlider(Qt::Horizontal, bx);
    context_budget_slider_->setRange(50, 1000);
    //single step
    context_budget_slider_->setSingleStep(50);
    context_budget_slider_->setPageStep(50);
    // add tick markers every 50
    context_budget_slider_->setTickInterval(50);
    context_budget_slider_->setTickPosition(QSlider::TicksBelow);
    context_budget_slider_->setValue(300);

    // label for slider
    budget_value_label_ = new QLabel("300", bx);
    budget_value_label_->setMinimumWidth(40);
    budget_value_label_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    build_context_button_= new QPushButton("Build Context", bx);

    // token budget
    auto* control_row = new QHBoxLayout;
    control_row->addWidget(new QLabel("Token budget:", bx));
    control_row->addWidget(context_budget_slider_, 1);
    control_row->addWidget(budget_value_label_);
    control_row->addWidget(build_context_button_);

    // read only output area
    context_text_ = new QPlainTextEdit(bx);
    context_text_->setReadOnly(true);
    context_text_->setPlaceholderText("Built context will appear here.");

    // add controls on top
    auto* bx_layout = new QVBoxLayout(bx);
    bx_layout->addLayout(control_row);
    bx_layout->addWidget(context_text_, 1);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(bx);

    // connect slider updates
    connect(context_budget_slider_, &QSlider::valueChanged, this, &ContextWidget::budget_changed);
    connect(build_context_button_, &QPushButton::clicked, this, &ContextWidget::request_context);

    // start state
    build_context_button_->setEnabled(false);
}

int ContextWidget::token_budget() const {
    return context_budget_slider_->value();
}

void ContextWidget::set_build_context_enabled(bool enabled) {
    build_context_button_->setEnabled(enabled);
}

void ContextWidget::set_context(const std::vector<ContextItem>& items) {
    if (items.empty()) {
        context_text_->setPlainText("No context items returned");
        return;
    }

    QStringList blocks;
    for (const ContextItem& item : items) {
        blocks << QString("Document: %1\nChunk: %2\nTokens: %3\n"
                            "Score: %4\nTruncated: %5\nText: %6")
                            .arg(QString::fromStdString(item.document_id))
                            .arg(static_cast<qulonglong>(item.chunk_sequence))
                            .arg(static_cast<qulonglong>(item.token_count))
                            .arg(QString::number(item.score, 'f', 4))
                            .arg(item.truncated ? "Yes" : "No")
                            .arg(QString::fromStdString(item.text));
    }
    // add divider between blocks
    context_text_->setPlainText(blocks.join("\n\n----------------------------------\n\n"));
}

void ContextWidget::clear_context() {
    context_text_->clear();
}

void ContextWidget::reset() {
    context_budget_slider_->setValue(300);
    clear_context();
    set_build_context_enabled(false);
}

void ContextWidget::request_context() {
    emit build_context_requested(token_budget());
}

void ContextWidget::budget_changed(int value) {
    const int snapped = ((value + 25) / 50) * 50;
    if (snapped != value) {
        context_budget_slider_->setValue(snapped);
        return;
    }
    budget_value_label_->setText(QString::number(value));
}

}  // namespace aiws::gui
