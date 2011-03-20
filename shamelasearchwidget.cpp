#include "shamelasearchwidget.h"
#include "ui_shamelasearchwidget.h"

#include "common.h"
#include "shamelasearcher.h"
#include "shamelaresultwidget.h"
#include "shamelamodels.h"
#include "tabwidget.h"
#include "searchfilterhandler.h"

#include <qmessagebox.h>
#include <qsettings.h>
#include <qprogressdialog.h>
#include <qevent.h>
#include <qabstractitemmodel.h>
#include <qsortfilterproxymodel.h>

ShamelaSearchWidget::ShamelaSearchWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::ShamelaSearchWidget)
{
    ui->setupUi(this);

    forceRTL(ui->lineQueryMust);
    forceRTL(ui->lineQueryShould);
    forceRTL(ui->lineQueryShouldNot);
    forceRTL(ui->lineFilter);

    m_shaModel = new ShamelaModels(this);
    m_filterHandler = new SearchFilterHandler(this);
    m_filterHandler->setShamelaModels(m_shaModel);
    m_filterText << "" << "" << "";

    ui->lineFilter->setMenu(m_filterHandler->getFilterLineMenu());

    loadSettings();

    connect(ui->lineQueryMust, SIGNAL(returnPressed()), SLOT(search()));
    connect(ui->lineQueryShould, SIGNAL(returnPressed()), SLOT(search()));
    connect(ui->lineQueryShouldNot, SIGNAL(returnPressed()), SLOT(search()));
    connect(ui->pushSearch, SIGNAL(clicked()), SLOT(search()));
}

ShamelaSearchWidget::~ShamelaSearchWidget()
{
    delete ui;
}

void ShamelaSearchWidget::closeEvent(QCloseEvent *e)
{
    saveSettings();
    e->accept();
}

void ShamelaSearchWidget::loadSettings()
{
    QSettings settings(SETTINGS_FILE, QSettings::IniFormat);
    m_resultParPage = settings.value("resultPeerPage", m_resultParPage).toInt();
    m_useMultiTab = settings.value("useTabs", true).toBool();

    ui->lineQueryMust->setText(settings.value("lastQueryMust").toString());
    ui->lineQueryShould->setText(settings.value("lastQueryShould").toString());
    ui->lineQueryShouldNot->setText(settings.value("lastQueryShouldNot").toString());

}

void ShamelaSearchWidget::saveSettings()
{
    qDebug("Save settings");

    QSettings settings(SETTINGS_FILE, QSettings::IniFormat);

    settings.setValue("lastQueryMust", ui->lineQueryMust->text());
    settings.setValue("lastQueryShould", ui->lineQueryShould->text());
    settings.setValue("lastQueryShouldNot", ui->lineQueryShouldNot->text());

    settings.setValue("resultPeerPage", m_resultParPage);
    settings.setValue("useTabs", m_useMultiTab);
}

void ShamelaSearchWidget::search()
{

    if(ui->lineQueryMust->text().isEmpty()){
        if(!ui->lineQueryShould->text().isEmpty()){
            ui->lineQueryMust->setText(ui->lineQueryShould->text());
            ui->lineQueryShould->clear();
        } else {
            QMessageBox::warning(this,
                                 trUtf8("البحث"),
                                 trUtf8("يجب ملء حقل العبارات التي يجب ان تظهر في النتائج"));
            return;
        }
    }

    QString mustQureyStr = ui->lineQueryMust->text();
    QString shouldQureyStr = ui->lineQueryShould->text();
    QString shouldNotQureyStr = ui->lineQueryShouldNot->text();

    normaliseSearchString(mustQureyStr);
    normaliseSearchString(shouldQureyStr);
    normaliseSearchString(shouldNotQureyStr);

    m_searchQuery = mustQureyStr + " " + shouldQureyStr;

    ArabicAnalyzer analyzer;
    BooleanQuery *q = new BooleanQuery;
    BooleanQuery *allFilterQuery = new BooleanQuery;
    QueryParser *queryPareser = new QueryParser(_T("text"), &analyzer);
    queryPareser->setAllowLeadingWildcard(true);

    try {
        if(!mustQureyStr.isEmpty()) {
            if(ui->checkQueryMust->isChecked())
                queryPareser->setDefaultOperator(QueryParser::AND_OPERATOR);
            else
                queryPareser->setDefaultOperator(QueryParser::OR_OPERATOR);

            Query *mq = queryPareser->parse(QStringToTChar(mustQureyStr));
            q->add(mq, BooleanClause::MUST);

        }

        if(!shouldQureyStr.isEmpty()) {
            if(ui->checkQueryShould->isChecked())
                queryPareser->setDefaultOperator(QueryParser::AND_OPERATOR);
            else
                queryPareser->setDefaultOperator(QueryParser::OR_OPERATOR);

            Query *mq = queryPareser->parse(QStringToTChar(shouldQureyStr));
            q->add(mq, BooleanClause::SHOULD);

        }

        if(!shouldNotQureyStr.isEmpty()) {
            if(ui->checkQueryShouldNot->isChecked())
                queryPareser->setDefaultOperator(QueryParser::AND_OPERATOR);
            else
                queryPareser->setDefaultOperator(QueryParser::OR_OPERATOR);

            Query *mq = queryPareser->parse(QStringToTChar(shouldNotQureyStr));
            q->add(mq, BooleanClause::MUST_NOT);
        }

        // Filtering
        if(ui->groupBoxFilter->isChecked()) {
            Query * filterQuery;
            bool required = ui->radioRequired->isChecked();
            bool prohibited = ui->radioProhibited->isChecked();
            bool gotAFilter = false;

            filterQuery = getBooksListQuery();
            if(filterQuery != NULL) {
                allFilterQuery->add(filterQuery, BooleanClause::SHOULD);
                gotAFilter = true;
            }

            filterQuery = getCatsListQuery();
            if(filterQuery != NULL) {
                allFilterQuery->add(filterQuery, BooleanClause::SHOULD);
                gotAFilter = true;
            }

            filterQuery = getAuthorsListQuery();
            if(filterQuery != NULL) {
                allFilterQuery->add(filterQuery, BooleanClause::SHOULD);
                gotAFilter = true;
            }

            if(gotAFilter) {
                allFilterQuery->setBoost(0.0);
                q->add(allFilterQuery, required, prohibited);
            }
        }
    } catch(CLuceneError &e) {
        if(e.number() == CL_ERR_Parse)
            QMessageBox::warning(this,
                                 trUtf8("خطأ في استعلام البحث"),
                                 trUtf8("هنالك خطأ في احدى حقول البحث"
                                        "\n"
                                        "تأكد من حذف الأقواس و المعقوفات وغيرها..."
                                        "\n"
                                        "او تأكد من أنك تستخدمها بشكل صحيح"));
        else
            QMessageBox::warning(0,
                                 "CLucene Error when Indexing",
                                 tr("Error code: %1\n%2").arg(e.number()).arg(e.what()));

        _CLDELETE(q);
        _CLDELETE(queryPareser);

        return;
    }
    catch(...) {
        QMessageBox::warning(0,
                             "Unkonw error when Indexing",
                             tr("Unknow error"));
        _CLDELETE(q);
        _CLDELETE(queryPareser);

        return;
    }
    ShamelaSearcher *m_searcher = new ShamelaSearcher;
    m_searcher->setBooksDb(m_booksDB);
    m_searcher->setIndexInfo(m_currentIndex);
    m_searcher->setQueryString(m_searchQuery);
    m_searcher->setQuery(q);
    m_searcher->setResultsPeerPage(m_resultParPage);

    ShamelaResultWidget *widget;
    int index=1;
    QString title = trUtf8("%1 (%2)")
                    .arg(m_currentIndex->name())
                    .arg(++m_searchCount);

    if(m_useMultiTab || m_tabWidget->count() < 2) {
        widget = new ShamelaResultWidget(this);
        index = m_tabWidget->addTab(widget, title);
    } else {
        widget = qobject_cast<ShamelaResultWidget*>(m_tabWidget->widget(index));
        widget->clearResults();
    }

    widget->setShamelaSearch(m_searcher);
    widget->setIndexInfo(m_currentIndex);
    widget->setBooksDb(m_booksDB);

    m_tabWidget->setCurrentIndex(index);
    m_tabWidget->setTabText(index, title);

    widget->doSearch();
}


Query *ShamelaSearchWidget::getBooksListQuery()
{
    int count = 0;
    BooleanQuery *q = new BooleanQuery();
    foreach(int id, m_shaModel->getSelectedBooks()) {
        TermQuery *term = new TermQuery(new Term(_T("bookid"), QStringToTChar(QString::number(id))));
        q->add(term,  BooleanClause::SHOULD);
        count++;
    }

    return count ? q : NULL;
}

Query *ShamelaSearchWidget::getCatsListQuery()
{
    int count = 0;
    BooleanQuery *q = new BooleanQuery();
    foreach(int id, m_shaModel->getSelectedCats()) {
        TermQuery *term = new TermQuery(new Term(_T("cat"), QStringToTChar(QString::number(id))));
        q->add(term,  BooleanClause::SHOULD);
        count++;
    }

    return count ? q : NULL;
}

Query *ShamelaSearchWidget::getAuthorsListQuery()
{
    int count = 0;
    BooleanQuery *q = new BooleanQuery();
    foreach(int id, m_shaModel->getSelectedAuthors()) {
        TermQuery *term = new TermQuery(new Term(_T("author"), QStringToTChar(QString::number(id))));
        q->add(term, BooleanClause::SHOULD);
        count++;
    }

    return count ? q : NULL;
}

void ShamelaSearchWidget::setIndexInfo(IndexInfo *info)
{
    m_currentIndex = info;
}

void ShamelaSearchWidget::setBooksDb(BooksDB *db)
{
    m_booksDB = db;
}

void ShamelaSearchWidget::setTabWidget(TabWidget *tabWidget)
{
    m_tabWidget = tabWidget;
}

void ShamelaSearchWidget::indexChanged()
{
    QProgressDialog progress(trUtf8("جاري انشاء مجالات البحث..."), QString(), 0, 4, this);
    progress.setModal(Qt::WindowModal);

    QStandardItemModel *catsModel = m_booksDB->getCatsListModel();
    PROGRESS_DIALOG_STEP("انشاء لائحة الأقسام");

    QStandardItemModel *booksModel = m_booksDB->getBooksListModel();
    PROGRESS_DIALOG_STEP("انشاء لائحة الكتب");

    QStandardItemModel *authModel = m_booksDB->getAuthorsListModel();
    PROGRESS_DIALOG_STEP("انشاء لائحة المؤلفيين");

    m_shaModel->setBooksListModel(booksModel);
    m_shaModel->setCatsListModel(catsModel);
    m_shaModel->setAuthorsListModel(authModel);

    ui->treeViewBooks->setModel(booksModel);
    ui->treeViewCats->setModel(catsModel);
    ui->treeViewAuthors->setModel(authModel);

    // Set the proxy model
    chooseProxy(ui->tabWidgetFilter->currentIndex());

    progress.setValue(progress.maximum());
}

void ShamelaSearchWidget::chooseProxy(int index)
{
    m_filterHandler->setFilterSourceModel(index);

    if(index == 0)
        ui->treeViewBooks->setModel(m_filterHandler->getFilterModel());

    else if(index == 1)
        ui->treeViewCats->setModel(m_filterHandler->getFilterModel());

    else
        ui->treeViewAuthors->setModel(m_filterHandler->getFilterModel());
}

void ShamelaSearchWidget::on_lineFilter_textChanged(QString text)
{
    m_filterText[ui->tabWidgetFilter->currentIndex()] = text;

    m_filterHandler->setFilterText(text);
}

void ShamelaSearchWidget::on_tabWidgetFilter_currentChanged(int index)
{
    chooseProxy(index);

    ui->lineFilter->setText(m_filterText.at(index));
}