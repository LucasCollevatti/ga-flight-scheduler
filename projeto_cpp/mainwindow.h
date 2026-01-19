// mainwindow.h
#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>

class QLineEdit;
class QPushButton;
class QSpinBox;
class QTextEdit;
class QLabel;
class QProgressBar;

#include "gaengine.h"

class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void browseAirports();
    void browseRoutes();
    void browsePassengers();
    void browseFleet();
    void browseForbidden();
    void runGA();

private:
    QLineEdit   *m_airportsEdit;
    QLineEdit   *m_routesEdit;
    QLineEdit   *m_passengersEdit;
    QLineEdit   *m_fleetEdit;
    QLineEdit   *m_forbiddenEdit;
    QSpinBox    *m_popSpin;
    QSpinBox    *m_genSpin;
    QTextEdit   *m_outputEdit;
    QLabel      *m_statusLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_runButton;

    GAEngine     m_engine;

    QString buildProgressLine(int gen, int maxGen, double bestScore) const;
};

#endif // MAINWINDOW_H
