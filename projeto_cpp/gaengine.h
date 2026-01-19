#ifndef GAENGINE_H
#define GAENGINE_H

#include <QObject>
#include <QVector>
#include <QJsonDocument>
#include <QSet>
#include <QtGlobal>
#include <functional>

struct Airport {
    int id;
    QString code;
    QString name;
    double lat;
    double lon;
};

struct Route {
    int id;
    int orig;
    int dest;
    int timeMin;  // TAB_TV (minutos, múltiplos de 60)
};

struct ODDemand {
    int orig;
    int dest;
    int demand;
};

struct FleetInfo {
    int numAircraft;
    int seatsPerAircraft;
    QStringList aircraftIds; // tamanho numAircraft
};

struct FlightTemplate {
    int id;      // índice global (gene)
    int routeId; // índice em routes[]
    int orig;
    int dest;
    int depMin;  // em minutos desde 00:00
    int arrMin;
};

struct FlightInstance {
    int tmplId;      // índice em allFlights
    int routeId;
    int orig;
    int dest;
    int depMin;
    int arrMin;
    int aircraftIdx; // índice da aeronave usada (-1 se sem aeronave)
    int capacity;    // assentos
    int usedSeats;   // pax alocados
};

struct EvalStats {
    int servedTotal = 0;
    int servedDirect = 0;
    int servedOneHop = 0;
    int unserved = 0;
    int numFlights = 0;
    int usedAircraft = 0;
    long long totalTravelTime = 0; // soma (arr - dep) * pax
};

struct GAResult {
    double bestScore = -1e9;
    QString summaryText;
    QJsonDocument fullJson;       // voos + resumo
    QJsonDocument passengersJson; // mapeamento OD->voos
    QJsonDocument evolutionJson;  // histórico geração a geração do GA
};

class GAEngine : public QObject
{
    Q_OBJECT
public:
    explicit GAEngine(QObject *parent = nullptr);

    bool loadData(const QString &airportsPath,
                  const QString &routesPath,
                  const QString &passengersPath,
                  const QString &fleetPath,
                  const QString &forbiddenPath,
                  QString &error);

    using ProgressCallback = std::function<void(int gen, double bestScore)>;

    GAResult runGA(int population, int generations,
                   ProgressCallback cbProgress = ProgressCallback());

private:
    // Dados de entrada
    QVector<Airport>    m_airports;
    QVector<Route>      m_routes;
    QVector<ODDemand>   m_od;
    FleetInfo           m_fleet;
    QVector<FlightTemplate> m_allFlights;  // universo de voos possíveis
    QSet<quint64>       m_forbiddenOD;     // pares (orig,dest) proibidos

    // GA
    QVector<QVector<bool>> initPopulation(int popSize, int numGenes);
    double evaluateChromosome(const QVector<bool> &chrom,
                              EvalStats &stats,
                              QVector<FlightInstance> &bestFlights,
                              QVector<QVariantMap> &paxAssignments) const;
    void crossover(const QVector<bool> &p1, const QVector<bool> &p2,
                   QVector<bool> &c1, QVector<bool> &c2, double pCross) const;
    void mutate(QVector<bool> &ind, double pMut) const;

    // helpers
    void buildAllFlights();
    QString formatTimeHHMM(int minutes) const;
    quint64 odKey(int orig, int dest) const;
};

#endif // GAENGINE_H
