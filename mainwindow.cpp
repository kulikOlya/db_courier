#include "mainwindow.h"
#include "ui_mainwindow.h"
#include "logindialog.h"
#include "commentdialog.h"
#include <QSqlDatabase>
#include <QSqlQueryModel>
#include <QItemSelectionModel>
#include <QSqlQuery>
#include <QSettings>
#include <QDebug>
#include <QMessageBox>
#include <QTimer>
#include <QKeySequence>
#include <QSqlError>
#include <QSqlRecord>

namespace
{
struct DBOpener
{
    DBOpener( MainWindow * const iMW)
    {
        bool isOpened = QSqlDatabase::database().open();
        qDebug() << "DBOpen: " << isOpened;
        if (!isOpened)
            QMessageBox::critical(iMW, "Database connection error", "Cannot establish connection to database");
    }
    ~DBOpener()
    {
        qDebug() << "~DBOpen: ";
        if (!QSqlDatabase::database().isOpen())
            QSqlDatabase::database().close();

    }
};
}

MainWindow::MainWindow(QWidget *parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , m_login(new LoginDialog(this))
  , m_commentDialog( new CommentDialog( this ))
  , m_courierID( 0 )
  , m_inputModel( new QSqlQueryModel( this ) )
  , m_inputSelectionModel( new QItemSelectionModel( m_inputModel, this ) )
  , m_selectedModel( new QSqlQueryModel( this ))
  , m_selectedSelectionModel( new QItemSelectionModel( m_selectedModel, this ))
{
    ui->setupUi(this);
    setupConnection();

    ui->tableView->setModel(m_inputModel);
    ui->tableView->setSelectionModel( m_inputSelectionModel );

    ui->selectedView->setModel( m_selectedModel);
    ui->selectedView->setSelectionModel( m_selectedSelectionModel);

    QTimer::singleShot(10, this, SLOT(processLogin()));
    connect( this, SIGNAL(updateInputView()), this, SLOT(redrawForSelect()));
    connect( this, SIGNAL(updateSelView()), this, SLOT(redrawSelected()) );
    connect( ui->actionRelogin, SIGNAL(triggered()), this, SLOT(processLogin()));
    connect( ui->actionDisconnect, SIGNAL(triggered()), this, SLOT(disconnectCourier()));

    connect( ui->actionSelect, SIGNAL(triggered()), this, SLOT(selectBook()));
    connect( ui->actionDeselect, SIGNAL(triggered()), this, SLOT(deselectBook()));
    connect( ui->actionMark_as_Delivered, SIGNAL(triggered()), this, SLOT(markBook()));
    connect( ui->actionChange_comment, SIGNAL(triggered()), this, SLOT(editComment()));
    connect( ui->tabWidget, SIGNAL(currentChanged(int)), this, SLOT(currentTabChanged(int)));

    connect( m_inputSelectionModel, SIGNAL(currentRowChanged(QModelIndex,QModelIndex)),
             this, SLOT(inputSelectionChanged(QModelIndex,QModelIndex)));
    connect( m_selectedSelectionModel, SIGNAL(currentRowChanged(QModelIndex,QModelIndex)),
             this, SLOT(selSelectionChanged(QModelIndex,QModelIndex)));
}

void MainWindow::editComment()
{
    const int row = m_selectedSelectionModel->currentIndex().row();
    if (-1 == row) {
        return;
    }

    const QString oldComment = ui->commentLabel->text();
    m_commentDialog->prepare( oldComment );

    if (QDialog::Accepted == m_commentDialog->exec()) {
        const QString& newComment = m_commentDialog->getComment();

        DBOpener db( this );

        const QSqlRecord& record = m_selectedModel->record( row );

        QSqlQuery query;
        if ( "Deliver" == record.value( 0 ) ) {
            qDebug() << "Prepare: " <<
                    query.prepare( "UPDATE book_to_deliver "
                                   "SET commnt = :newc "
                                   "WHERE purchasing_date = to_timestamp(:dt, 'J SSSSS') "
                                     "AND isbn = :isbn "
                                     "AND customer_id = :cust"
                                   );
        }
        else {
            qDebug() << "Prepare: " <<
                    query.prepare( "UPDATE book_to_receive "
                                   "SET commnt = :newc "
                                   "WHERE purchasing_date = to_timestamp(:dt, 'J SSSSS') "
                                     "AND isbn = :isbn "
                                     "AND customer_id = :cust"
                                   );
        }

        query.bindValue( ":newc", newComment );
        query.bindValue( ":dt", record.value( 1 ));
        query.bindValue( ":cust", record.value( 2 ));
        query.bindValue( ":isbn", record.value( 3 ));

        qDebug() << "Transaction: " <<
                    QSqlDatabase::database().transaction();

        qDebug() << "Exec: " << query.exec();
        const bool commit = QSqlDatabase::database().commit();
        qDebug() << "Commit: " << commit;
        if (!commit) {
            qDebug() << "Rollback: " <<
                        QSqlDatabase::database().rollback();
        }
        else {
            ui->commentLabel->setText( newComment );
            emit updateSelView();
        }
    }
}

void MainWindow::selSelectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    const int curr = current.row();
    qDebug() << curr << previous.row();
    if (curr == previous.row()) {
        return;
    }

    ui->actionChange_comment->setEnabled( -1 != curr );
    ui->actionDeselect->setEnabled( -1 != curr );
    ui->actionMark_as_Delivered->setEnabled( -1 != curr);
    ui->pushButton_2->setEnabled( -1 != curr );
    ui->pushButton_3->setEnabled( -1 != curr );
    ui->pushButton_4->setEnabled( -1 != curr );

    if (-1 != curr) { // load comment
        ui->commentLabel->setText( m_selectedModel->record( curr ).value( 8 ).toString() );
    }
    else {
        ui->commentLabel->clear();
    }
}

void MainWindow::inputSelectionChanged(const QModelIndex &current, const QModelIndex &previous)
{
    qDebug() << current.row() << previous.row();
    const int curr = current.row();
    if (curr == previous.row()) {
        return;
    }

    ui->actionSelect->setEnabled( -1 != curr );
    ui->pushButton->setEnabled( -1 != curr);
}

void MainWindow::currentTabChanged(const int tab)
{
    ui->actionChange_comment->setEnabled( false );
    ui->actionDeselect->setEnabled( false );
    ui->actionMark_as_Delivered->setEnabled( false );
    ui->actionSelect->setEnabled( false );
    ui->pushButton->setEnabled( false );
    ui->pushButton_2->setEnabled( false );
    ui->pushButton_3->setEnabled( false );
    ui->pushButton_4->setEnabled( false );
    switch (tab) {
    case 0: // input tab
        // TODO: start another timer?
        emit updateInputView();
        break;
    case 1:
        //TODO: start another timer?
        emit updateSelView();
        break;
    }
}

void MainWindow::redrawForSelect()
{
    DBOpener db( this );

    QSqlQuery query;
    qDebug() << "PREPARE: " <<
                query.prepare( "SELECT h.dr"
                                    ", to_char(h.purchasing_date, 'J SSSSS')"
                                    ", h.customer_id"
                                    ", h.isbn"
                                    ", h.address"
                                    ", book.title"
                                    ", c.name "
                                    ", c.phone "
                                     "FROM "
                                        "("
                                          "SELECT 'Receive' dr"
                                               ", b.purchasing_date"
                                               ", b.isbn"
                                               ", b.customer_id"
                                               ", b.address "
                                          "FROM book_to_receive b "
                                               "LEFT JOIN receiving d ON "
                                                     "b.purchasing_date = d.purchasing_date "
                                                 "AND b.isbn = d.isbn "
                                                 "AND b.customer_id = d.customer_id "
                                          "WHERE courier_id IS NULL "
                                        "UNION "
                                          "SELECT 'Deliver' dr"
                                               ", b.purchasing_date"
                                               ", b.isbn"
                                               ", b.customer_id"
                                               ", b.address "
                                          "FROM book_to_deliver b "
                                               "LEFT JOIN delivery d ON "
                                                    "b.purchasing_date = d.purchasing_date "
                                                "AND b.isbn = d.isbn "
                                                "AND b.customer_id = d.customer_id "
                                          "WHERE courier_id IS NULL"
                                        ") h "
                                          "JOIN book ON "
                                               "book.isbn = h.isbn "
                                          "JOIN customer c ON "
                                               "c.customer_id = h.customer_id"
                                     );
    qDebug() << "Exec: " << query.exec();
    m_inputModel->setQuery( query);
    m_inputModel->setHeaderData( 0, Qt::Horizontal, tr("Receive/Deliver"));
    m_inputModel->setHeaderData( 1, Qt::Horizontal, tr("Date of purchase"));
    m_inputModel->setHeaderData( 2, Qt::Horizontal, tr("CustomerID"));
    m_inputModel->setHeaderData( 3, Qt::Horizontal, tr("ISBN"));
    m_inputModel->setHeaderData( 4, Qt::Horizontal, tr("Address"));
    m_inputModel->setHeaderData( 5, Qt::Horizontal, tr("Title"));
    m_inputModel->setHeaderData( 6, Qt::Horizontal, tr("Customer's name"));
    m_inputModel->setHeaderData( 7, Qt::Horizontal, tr("Customer's phone"));
    ui->tableView->hideColumn( 1 ); // date of purchase
    ui->tableView->hideColumn( 2 ); // customer id
    ui->tableView->hideColumn( 7 ); // phonev
    ui->tableView->resizeColumnsToContents();
    qDebug() << m_inputModel->rowCount();
}

void MainWindow::redrawSelected()
{
    if (0 == m_courierID) {
        qDebug() << "No courier is logined.";
        return;
    }

    DBOpener db( this );

    QSqlQuery query;
    qDebug() << "Prepare: " <<
                query.prepare(QString(
                             "SELECT h.dr"
                                  ", to_char( h.purchasing_date, 'J SSSSS')"
                                  ", h.customer_id"
                                  ", h.isbn"
                                  ", h.address"
                                  ", book.title"
                                  ", c.name"
                                  ", c.phone "
                                  ", h.commnt "
                             "FROM "
                                "("
                                  "SELECT 'Receive' dr"
                                       ", b.purchasing_date"
                                       ", b.isbn"
                                       ", b.customer_id"
                                       ", b.address"
                                       ", b.commnt "
                                  "FROM book_to_receive b "
                                       "JOIN receiving d ON "
                                             "b.purchasing_date = d.purchasing_date "
                                         "AND b.isbn = d.isbn "
                                         "AND b.customer_id = d.customer_id "
                                  "WHERE d.courier_id = %0 "
                                "UNION "
                                  "SELECT 'Deliver' dr"
                                       ", b.purchasing_date"
                                       ", b.isbn"
                                       ", b.customer_id"
                                       ", b.address"
                                       ", b.commnt "
                                  "FROM book_to_deliver b "
                                       "JOIN delivery d ON "
                                            "b.purchasing_date = d.purchasing_date "
                                        "AND b.isbn = d.isbn "
                                        "AND b.customer_id = d.customer_id "
                                  "WHERE d.courier_id = %1 "
                                ") h "
                                  "JOIN book ON "
                                       "book.isbn = h.isbn "
                                  "JOIN customer c ON "
                                       "c.customer_id = h.customer_id"
                                  ).arg(m_courierID).arg(m_courierID));

    qDebug() << "Exec: " << query.exec();
    m_selectedModel->setQuery( query );
    qDebug() << m_selectedModel->rowCount();
    m_selectedModel->setHeaderData( 0, Qt::Horizontal, tr("Receive/Deliver"));
    m_selectedModel->setHeaderData( 1, Qt::Horizontal, tr("Date of purchase"));
    m_selectedModel->setHeaderData( 2, Qt::Horizontal, tr("CustomerID"));
    m_selectedModel->setHeaderData( 3, Qt::Horizontal, tr("ISBN"));
    m_selectedModel->setHeaderData( 4, Qt::Horizontal, tr("Address"));
    m_selectedModel->setHeaderData( 5, Qt::Horizontal, tr("Title"));
    m_selectedModel->setHeaderData( 6, Qt::Horizontal, tr("Customer's name"));
    m_selectedModel->setHeaderData( 7, Qt::Horizontal, tr("Customer's phone"));
    m_selectedModel->setHeaderData( 8, Qt::Horizontal, tr("Comment"));
    ui->selectedView->hideColumn( 1 ); // date of purchase
    ui->selectedView->hideColumn( 2 ); // customer id
    ui->selectedView->hideColumn( 8 ); // comment
    ui->selectedView->resizeColumnsToContents();
}

void MainWindow::selectBook()
{
    const int row = m_inputSelectionModel->currentIndex().row();

    if (-1 == row) {
        qDebug() << "No row is selected";
        return;
    }

    const QSqlRecord record = m_inputModel->record( row );
    const QString customerID = record.value( 2 ).toString();
    qDebug() << customerID;
    const QString purchasingDate = record.value( 1 ).toString();
    qDebug() << purchasingDate;
    const QString isbn = record.value( 3 ).toString();
    qDebug() << isbn;

    DBOpener dbopener( this );

    QSqlQuery query;
    qDebug() << "Prepare: " <<
                query.prepare( "CALL courier_book_select( :isbn, to_timestamp(:dt, 'J SSSSS'), :cust, :cour)" );

    query.bindValue( ":isbn", isbn);
    query.bindValue( ":dt", purchasingDate);
    query.bindValue( ":cust", customerID);
    query.bindValue( ":cour", m_courierID);

    qDebug() << "Transaction" << QSqlDatabase::database().transaction();
    qDebug() << "Exec: " << query.exec();
    const bool commit = QSqlDatabase::database().commit();
    qDebug() << "Commit: " << commit;
    if (!commit) {
        qDebug() << "Rollback: " << QSqlDatabase::database().rollback();
    }
    else {
        ui->actionSelect->setEnabled( false );
        ui->pushButton->setEnabled( false );
        emit updateInputView();
    }
}

void MainWindow::deselectBook()
{
    const int row = m_selectedSelectionModel->currentIndex().row();

    if (-1 == row) {
        qDebug() << "No row is selected";
        return;
    }

    const QSqlRecord record = m_selectedModel->record( row );
    const QString customerID = record.value( 2 ).toString();
    qDebug() << customerID;
    const QString purchasingDate = record.value( 1 ).toString();
    qDebug() << purchasingDate;
    const QString isbn = record.value( 3 ).toString();
    qDebug() << isbn;

    DBOpener dbopener( this );

    QSqlQuery query;
    qDebug() << "Prepare: " <<
                query.prepare( "CALL courier_book_deselect( :isbn, to_timestamp(:dt, 'J SSSSS'), :cust, :cour)" );

    query.bindValue( ":isbn", isbn);
    query.bindValue( ":dt", purchasingDate);
    query.bindValue( ":cust", customerID);
    query.bindValue( ":cour", m_courierID);

    qDebug() << "Transaction" << QSqlDatabase::database().transaction();
    qDebug() << "Exec: " << query.exec();
    const bool commit = QSqlDatabase::database().commit();
    qDebug() << "Commit: " << commit;
    if (!commit) {
        qDebug() << "Rollback: " << QSqlDatabase::database().rollback();
    }
    else {
        emit updateSelView();
    }
}

void MainWindow::markBook()
{
    const int row = m_selectedSelectionModel->currentIndex().row();

    if (-1 == row) {
        qDebug() << "No row is selected";
        return;
    }

    const QSqlRecord record = m_selectedModel->record( row );
    const QString customerID = record.value( 2 ).toString();
    qDebug() << customerID;
    const QString purchasingDate = record.value( 1 ).toString();
    qDebug() << purchasingDate;
    const QString isbn = record.value( 3 ).toString();
    qDebug() << isbn;

    DBOpener dbopener( this );

    QSqlQuery query;
    qDebug() << "Prepare: " <<
                query.prepare( "CALL courier_mark_book( :isbn, to_timestamp(:dt, 'J SSSSS'), :cust, :cour)" );

    query.bindValue( ":isbn", isbn);
    query.bindValue( ":dt", purchasingDate);
    query.bindValue( ":cust", customerID);
    query.bindValue( ":cour", m_courierID);

    qDebug() << "Transaction" << QSqlDatabase::database().transaction();
    qDebug() << "Exec: " << query.exec();
    const bool commit = QSqlDatabase::database().commit();
    qDebug() << "Commit: " << commit;
    if (!commit) {
        qDebug() << "Rollback: " << QSqlDatabase::database().rollback();
    }
    else {
        emit updateSelView();
    }
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::processLogin()
{
    disconnectCourier();

    m_login->clear();
    if (QDialog::Accepted == m_login->exec())
    {
        DBOpener olya( this );

        QSqlQuery searchPasswordHash;
        qDebug() << "Prepare: " <<
                    searchPasswordHash.prepare( "SELECT COUNT(*) "
                                                "FROM courier "
                                                "WHERE courier_id = :courierID "
                                                "AND password_hash = :passwordHash "
                                                );
        searchPasswordHash.bindValue( ":courierID", m_login->userName() );
        searchPasswordHash.bindValue( ":passwordHash", m_login->passwordHash() );

        qDebug() << "Exec: " << searchPasswordHash.exec();
        qDebug() << "Error: " << searchPasswordHash.lastError();
        qDebug() << "First: "<< searchPasswordHash.first();

        const uint found = searchPasswordHash.value( 0 ).toUInt();

        if (1 == found)
        {
            m_courierID = m_login->userName().toUInt();
            connectCourier();
        }
        else
        {
            m_courierID = 0;
            if (QMessageBox::Retry ==
                    QMessageBox::critical( this
                                           , tr("Login error")
                                           , tr("User with provided credentials does not exist! Retry?")
                                           , QMessageBox::Retry | QMessageBox::Cancel)
                    )
                QTimer::singleShot(10, this, SLOT(processLogin()));
        }
    }
}


void MainWindow::setupConnection() const
{
    QSettings settings( "settings.ini", QSettings::IniFormat );

    settings.beginGroup( "database" );
    const QString dbDriver( settings.value( "driver",   "QOCI"      ).toString() );
    const QString hostName( settings.value( "hostname", "localhost" ).toString() );
    const QString dbName( settings.value( "database", "bookstore" ).toString() );
    const QString userName( settings.value( "user",     QString()   ).toString() );
    const QString password( settings.value( "password", QString()   ).toString() );
    const int port( settings.value( "port", "1521").toInt());
    settings.endGroup();

    qDebug() << "driver: " << dbDriver;
    qDebug() << "hostname: " << hostName;
    qDebug() << "database: " << dbName;
    qDebug() << "username: " << userName;
    qDebug() << "password: " << password;
    qDebug() << "port: " << port;

    QSqlDatabase db = QSqlDatabase::addDatabase( dbDriver );
    db.setHostName(     hostName );
    db.setDatabaseName( dbName );
    db.setUserName(     userName );
    db.setPassword(     password );
    db.setPort( port );
}

void MainWindow::disconnectCourier()
{
    m_inputModel->clear();
    m_selectedModel->clear();
    ui->tabWidget->setEnabled( false );
    ui->menuAction->setEnabled( false );
    ui->actionDisconnect->setEnabled( false );

    m_courierID = 0;
}

void MainWindow::connectCourier()
{

    if (0 == m_courierID)
    {
        qDebug() << "Not connected.";
        return;
    }

    ui->tabWidget->setEnabled( true );
    ui->menuAction->setEnabled( true );

    ui->actionDisconnect->setEnabled( true );
    ui->tabWidget->setCurrentIndex( -1 );
    ui->tabWidget->setCurrentIndex( 0 );

    emit updateInputView();
}
