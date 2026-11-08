#include "aiws/gui/main_window.hpp"

#include "aiws/document.hpp"
#include "aiws/gui/resettable.hpp"
#include "aiws/gui/context_widget.hpp"
#include "aiws/gui/search_result_model.hpp"
#include "aiws/gui/term_inspector_widget.hpp"
#include "aiws/gui/workspace_widget.hpp"
#include "aiws/gui/search_widget.hpp"
#include "aiws/gui/build_runner.hpp"

#include <QFile>
#include <QFileDialog>
#include <QLineEdit>
#include <QFileInfo>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include <QLabel>
#include <algorithm>

namespace aiws::gui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    // TODO: Construct the M3 application window, compose the GUI components,
    // establish layouts/presentation, connect events, and establish initial state.

    // owns the backend, builds the window, and handles every event
    setWindowTitle("AI Workspace");
    resize(900, 900);

    auto* central = new QWidget(this);
    setCentralWidget(central);

    auto* header = new QLabel("AI Workspace", central);
    header->setObjectName("appHeader");
    header->setStyleSheet("#appHeader { background-color: #2e0707; color: white;"
                            " font-size: 18px; font-weight: bold; padding: 10px 14px; }");

    // four components
    workspace_widget_ = new WorkspaceWidget(central);
    search_widget_ = new SearchWidget(central);
    term_inspector_widget_ = new TermInspectorWidget(central);
    context_widget_ = new ContextWidget(central);

    auto* bottom_pair = new QHBoxLayout;
    bottom_pair->addWidget(term_inspector_widget_, 2);
    bottom_pair->addWidget(context_widget_, 3);

    status_label_ = new QLabel("Ready", central);
    reset_button_ = new QPushButton("Reset", central);
    exit_button_ = new QPushButton("Exit", central);

    reset_button_->setStyleSheet("QPushButton { background-color: #f2c14e; color: #1a1a1a; border: 1px solid #b8901f;"
                                    " border-radius: 4px; padding: 6px 18px; }"
                                    "QPushButton:hover { background-color: #f5cf72; }");
    exit_button_->setStyleSheet("QPushButton { background-color: #274fe2; color: #f2f0f0; border: 1px solid #274fe2;"
                                    " border-radius: 4px; padding: 6px 18px; }"
                                    "QPushButton:hover { background-color: #3b5dda; }");
    
    auto* status_row = new QHBoxLayout;
    status_row->addWidget(new QLabel("Status:", central));
    status_row->addWidget(status_label_, 1);
    status_row->addWidget(reset_button_);
    status_row->addWidget(exit_button_);

    // stack everything top to bottom
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(header);
    layout->addWidget(workspace_widget_, 3);
    layout->addWidget(search_widget_, 4);
    layout->addLayout(bottom_pair, 3);
    layout->addLayout(status_row);

    // connect component signals
    connect(workspace_widget_, &WorkspaceWidget::add_document_requested, this, &MainWindow::add_document);
    connect(workspace_widget_, &WorkspaceWidget::build_corpus_requested, this, &MainWindow::build_corpus);
    connect(workspace_widget_, &WorkspaceWidget::document_selected, this, &MainWindow::document_selected);

    connect(search_widget_, &SearchWidget::search_requested, this, &MainWindow::search_requested);
    connect(search_widget_, &SearchWidget::query_changed, this, &MainWindow::query_changed);
    connect(search_widget_, &SearchWidget::source_document_requested, this, &MainWindow::source_document_requested);

    connect(search_widget_, &SearchWidget::selected_chunk_changed, term_inspector_widget_, &TermInspectorWidget::set_selected_chunk);
    connect(term_inspector_widget_, &TermInspectorWidget::term_inspection_requested, this, &MainWindow::term_inspection_requested);
    connect(context_widget_, &ContextWidget::build_context_requested, this, &MainWindow::build_context_requested);

    connect(reset_button_, &QPushButton::clicked, this, &MainWindow::reset_application);
    connect(exit_button_, &QPushButton::clicked, this, &MainWindow::exit_application);

    // create the runner
    build_runner_ = new BuildRunner(this);
    connect(build_runner_, &BuildRunner::build_finished, this, &MainWindow::on_build_finished);
    status_label_->setObjectName("statusLabel");

    // startup
    update_action_states();
}

void MainWindow::set_exit_callback(ExitCallback callback) noexcept {
    exit_callback_ = callback;
}

// MainWindow adds bool add_document_file(const QString& path);, which does exactly 
// what Add Document does and returns false, changing nothing, if the file is unreadable, 
// and void set_build_delay_ms(int ms_per_document);
void MainWindow::add_document() {
    const QString path = QFileDialog::getOpenFileName(this, "Add Document", QString(), "Text files (*.txt);;All files (*)");
    if (path.isEmpty()) {
        return; // cancelled or no file selected
    }
    add_document_file(path);
}

bool MainWindow::add_document_file(const QString& path) {  // everything from add_document() except the file dialog, so it can be called from M4
    const QFileInfo info(path);
    const QString title = info.fileName();
    const std::string id = info.absoluteFilePath().toStdString();
    
    const std::vector<Document>& docs = workspace_.documents(); // the absolute path is the doc ID, so check for duplicates
    if (std::any_of(docs.begin(), docs.end(), [&id](const Document& d) {return d.id() == id;})) {
        status_label_ ->setText(QString("Document \"%1\" is already in the workspace.").arg(title));
        return false; // nothing will change
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        status_label_->setText(QString("Could not open document \"%1\".").arg(title));
        return false; // document is unreadable, so nothing will change
    }

    workspace_.add_document(Document{id, title.toStdString(), file.readAll().toStdString()});
    workspace_widget_->add_document(title);
    invalidate_corpus();
    status_label_->setText(QString("Added document \"%1\". Build corpus to include it").arg(title));
    return true; 
}

void MainWindow::set_build_delay_ms(int ms_per_document) {
    build_delay_ms_ = ms_per_document;
}

void MainWindow::build_corpus() {
    if (workspace_.documents().empty() || build_runner_->busy()) {
        return;
    }

    build_generation_++; // this build's ticket

    // workspace_ is copied here on the GUI thread and the worker gets the copy
    if (!build_runner_->start(build_generation_, workspace_, build_delay_ms_)) {
        return;
    }
    corpus_state_ = CorpusState::Building;
    clear_processing_outputs();
    workspace_widget_->set_corpus_status("Building...");
    status_label_->setText(QString("Now building corpus from %1 document(s)...")
        .arg(static_cast<qulonglong>(workspace_.documents().size())));
    
    update_action_states();
}

void MainWindow::document_selected(int row) {
    const auto& documents = workspace_.documents();
    if (row < 0 || static_cast<std::size_t>(row) >= documents.size()) {
        workspace_widget_->clear_selected_document(); // out of range so nothing
        return;
    }
    const Document& doc = documents[static_cast<std::size_t>(row)];
    workspace_widget_->show_selected_document(QString::fromStdString(doc.title()),
                                                QString::fromStdString(doc.text()));
}

void MainWindow::search_requested(const QString& query, int k) {
    // a good query has no leading or trailing spaces
    const QString g_query = query.trimmed();
    if (corpus_state_ != CorpusState::Built || g_query.isEmpty()) {
        return; 
    }

    std::vector<SearchResult> results;
    try {
        results = processing_core_.search(g_query.toStdString(), k);
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Search", QString::fromStdString(e.what()));
        status_label_->setText("Search failed.");
        return;
    }

    const auto count = static_cast<qulonglong>(results.size());
    search_widget_->set_results(std::move(results));
    context_widget_->clear_context(); // remove old search context
    status_label_->setText(QString("Search returned %1 results").arg(count));
}

void MainWindow::query_changed(const QString& query) {
    (void)query; // has_query() reads the field
    update_action_states();
}

void MainWindow::build_context_requested(int token_budget) {
    const QString g_query = search_widget_->query().trimmed();
    if (corpus_state_ != CorpusState::Built || g_query.isEmpty()) {
        return; 
    }

    std::vector<ContextItem> items;
    try {
        items = processing_core_.build_context(g_query.toStdString(), search_widget_->result_count(),
                                                static_cast<std::size_t>(token_budget));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Build Context", QString::fromStdString(e.what()));
        status_label_->setText("Context Build Failed");
        return;
    }

    context_widget_->set_context(items);
    status_label_->setText(QString("Context Built: %1 items, budget %2 tokens")
                                    .arg(static_cast<qulonglong>(items.size()))
                                    .arg(token_budget));
}

void MainWindow::source_document_requested(const QString& document_id) {
    // find the document with the id provided
    const std::string id = document_id.toStdString();
    const auto& docs = workspace_.documents();
    for (std::size_t i = 0; i < docs.size(); i++) {
        if (docs[i].id() == id) {
            workspace_widget_->select_document(static_cast<int>(i));
            document_selected(static_cast<int>(i));
            status_label_->setText(QString("Showing source document %1.")
                                    .arg(QString::fromStdString(docs[i].title())));
            return;
        }
    }
    status_label_->setText("Source document not found in Workspace");
}

void MainWindow::term_inspection_requested(const QString& term,
                                           const QString& chunk_id) {
    const QString g_term = term.trimmed();
    if (corpus_state_ != CorpusState::Built || g_term.isEmpty()) {
        return; 
    }

    try {
        const std::string t = g_term.toStdString();
        // DF only needs the corpus
        const std::size_t df = processing_core_.document_frequency(t);
        // TF needs a selected chunk
        const bool has_chunk = !chunk_id.isEmpty();
        const std::size_t tf = has_chunk ? processing_core_.term_frequency(t, chunk_id.toStdString()) : 0;
        term_inspector_widget_->set_statistics(df, has_chunk, tf);
        status_label_->setText(QString("Inspected term \"%1\".").arg(g_term));
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Inspect Term", QString::fromStdString(e.what()));
        status_label_->setText("Term Inspection Failed");
    }
}

void MainWindow::reset_application() {
    // backend first
    workspace_.clear();
    processing_core_ = ProcessingCore{};
    corpus_state_ = CorpusState::NotBuilt;

    // polymorphism
    Resettable* const components[] = {workspace_widget_, search_widget_, term_inspector_widget_, context_widget_};
    for (Resettable* component : components) {
        component->reset();
    }

    update_action_states();
    status_label_->setText("Application reset.");
}

void MainWindow::exit_application() {
    if (exit_callback_ != nullptr) {
        exit_callback_(this);
    }
}

void MainWindow::update_action_states() {
    // decides whihc action buttons are enabled
    const bool has_docs = !workspace_.documents().empty();
    const bool has_query = search_widget_->has_query();

    workspace_widget_->set_build_corpus_enabled(has_docs && !build_runner_->busy());  // second build can't start while a thread exists
    search_widget_->set_search_enabled(corpus_state_ == CorpusState::Built && has_query);
    term_inspector_widget_->set_inspection_enabled(corpus_state_ == CorpusState::Built);
    context_widget_->set_build_context_enabled(corpus_state_ == CorpusState::Built && has_query);
}

void MainWindow::invalidate_corpus() {
    if (corpus_state_ != CorpusState::NotBuilt) {
        corpus_state_ = CorpusState::Stale;
        workspace_widget_->set_corpus_status("Corpus: Old Corpus (rebuild required)"); // check for stale corpus
    }
    
    build_generation_++; 
    build_runner_->cancel();

    clear_processing_outputs();
    update_action_states();
}

void MainWindow::clear_processing_outputs() {
    // clear all displays compluted from a corpus
    search_widget_->clear_results();
    term_inspector_widget_->set_selected_chunk(QString());
    term_inspector_widget_->clear_statistics();
    context_widget_->clear_context();
}

void MainWindow::on_build_finished(quint64 generation) {
    // valid only if nothing has changed the workspace since the build started
    const bool valid = (generation == build_generation_);

    try {
        ProcessingCore built = build_runner_->take_result(); // or rethrows

        if (valid) {
            processing_core_ = std::move(built); // moves and destroys prev corpus
            corpus_state_ = CorpusState::Built;
            const auto chunks = static_cast<qulonglong>(processing_core_.chunk_count());
            workspace_widget_->set_corpus_status(QString("Corpus: %1 chunks").arg(chunks));
            status_label_->setText(QString("Corpus built: %1 chunks.").arg(chunks));
        }
        // if not valid built is destroyed at the end of block
    } catch (const std::exception& error) {
        if (valid) { // cancelled or outdated build is not reported as a failure
            corpus_state_ = CorpusState::Stale;
            workspace_widget_->set_corpus_status("Build has failed");
            status_label_->setText(QString("Build has failed: %1")
                .arg(error.what()));
        }
    }
    update_action_states();
}

}  // namespace aiws::gui
