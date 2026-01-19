#include "gaengine.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QRandomGenerator>
#include <QtMath>
#include <QSet>
#include <QElapsedTimer>

GAEngine::GAEngine(QObject *parent)
    : QObject(parent)
{
}

bool GAEngine::loadData(const QString &airportsPath,
                        const QString &routesPath,
                        const QString &passengersPath,
                        const QString &fleetPath,
                        const QString &forbiddenPath,
                        QString &error)
{
    m_airports.clear();
    m_routes.clear();
    m_od.clear();
    m_allFlights.clear();
    m_fleet = FleetInfo();
    m_forbiddenOD.clear();

    auto loadJson = [](const QString &path, QJsonDocument &doc, QString &err) -> bool {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) {
            err = QString("Cannot open %1").arg(path);
            return false;
        }
        QByteArray data = f.readAll();
        f.close();
        QJsonParseError pe;
        doc = QJsonDocument::fromJson(data, &pe);
        if (pe.error != QJsonParseError::NoError) {
            err = QString("JSON parse error in %1: %2")
            .arg(path, pe.errorString());
            return false;
        }
        return true;
    };

    QJsonDocument docAir, docRt, docPax, docFleet, docForbidden;

    if (!loadJson(airportsPath, docAir, error)) return false;
    if (!loadJson(routesPath, docRt, error)) return false;
    if (!loadJson(passengersPath, docPax, error)) return false;
    if (!loadJson(fleetPath, docFleet, error)) return false;
    if (!loadJson(forbiddenPath, docForbidden, error)) return false;

    // airports.json
    QJsonArray arrAir = docAir.object().value("airports").toArray();
    if (arrAir.isEmpty()) {
        error = "airports.json: missing 'airports' array";
        return false;
    }
    for (const QJsonValue &v : arrAir) {
        QJsonObject o = v.toObject();
        Airport a;
        a.id   = o.value("id").toInt();
        a.code = o.value("code").toString();
        a.name = o.value("name").toString();
        a.lat  = o.value("lat").toDouble();
        a.lon  = o.value("lon").toDouble();
        m_airports.append(a);
    }

    // routes.json
    QJsonArray arrRt = docRt.object().value("routes").toArray();
    if (arrRt.isEmpty()) {
        error = "routes.json: missing 'routes' array";
        return false;
    }
    for (const QJsonValue &v : arrRt) {
        QJsonObject o = v.toObject();
        Route r;
        r.id      = o.value("id").toInt();
        r.orig    = o.value("orig_id").toInt();
        r.dest    = o.value("dest_id").toInt();
        r.timeMin = o.value("time_min").toInt();
        m_routes.append(r);
    }

    // passengers_od.json
    QJsonArray arrPax = docPax.object().value("od_pairs").toArray();
    if (arrPax.isEmpty()) {
        error = "passengers_od.json: missing 'od_pairs' array";
        return false;
    }
    for (const QJsonValue &v : arrPax) {
        QJsonObject o = v.toObject();
        ODDemand d;
        d.orig   = o.value("orig_id").toInt();
        d.dest   = o.value("dest_id").toInt();
        d.demand = o.value("demand").toInt();
        m_od.append(d);
    }

    // fleet.json
    QJsonObject objF = docFleet.object();
    m_fleet.numAircraft      = objF.value("num_aircraft").toInt();
    m_fleet.seatsPerAircraft = objF.value("seats_per_aircraft").toInt();
    QJsonArray arrIds = objF.value("aircraft_ids").toArray();
    for (const QJsonValue &v : arrIds) {
        m_fleet.aircraftIds.append(v.toString());
    }
    if (m_fleet.aircraftIds.size() < m_fleet.numAircraft) {
        for (int i = m_fleet.aircraftIds.size(); i < m_fleet.numAircraft; ++i)
            m_fleet.aircraftIds.append(QString("AC_%1").arg(i, 3, 10, QChar('0')));
    }

    // forbidden_routes.json (pares OD proibidos)
    QJsonArray arrForb = docForbidden.object().value("forbidden_od").toArray();
    for (const QJsonValue &v : arrForb) {
        QJsonObject o = v.toObject();
        int orig = o.value("orig_id").toInt();
        int dest = o.value("dest_id").toInt();
        if (orig >= 0 && dest >= 0) {
            m_forbiddenOD.insert(odKey(orig, dest));
        }
    }

    buildAllFlights();

    if (m_allFlights.isEmpty()) {
        error = "No feasible flights built from routes/slots.";
        return false;
    }

    return true;
}

void GAEngine::buildAllFlights()
{
    m_allFlights.clear();
    const int DAY_START = 6 * 60;
    const int DAY_END   = 22 * 60;
    const int SLOT_MIN  = 60;

    int idCounter = 0;
    for (const Route &r : m_routes) {
        // ignora completamente rotas proibidas: nunca geram genes
        if (m_forbiddenOD.contains(odKey(r.orig, r.dest)))
            continue;
        for (int dep = DAY_START; dep <= (21 * 60); dep += SLOT_MIN) {
            int arr = dep + r.timeMin;
            if (arr > DAY_END) continue;
            FlightTemplate ft;
            ft.id      = idCounter++;
            ft.routeId = r.id;
            ft.orig    = r.orig;
            ft.dest    = r.dest;
            ft.depMin  = dep;
            ft.arrMin  = arr;
            m_allFlights.append(ft);
        }
    }
}

quint64 GAEngine::odKey(int orig, int dest) const
{
    return ( (quint64)orig << 32 ) | (quint64)(dest & 0xffffffff);
}

QVector<QVector<bool>> GAEngine::initPopulation(int popSize, int numGenes)
{
    QVector<QVector<bool>> pop;
    pop.reserve(popSize);
    QRandomGenerator *rng = QRandomGenerator::global();

    for (int i = 0; i < popSize; ++i) {
        QVector<bool> ind(numGenes);
        for (int g = 0; g < numGenes; ++g) {
            // probabilidade baixa de ativar voo (começa esparso)
            ind[g] = (rng->generateDouble() < 0.02);
        }
        pop.append(ind);
    }
    return pop;
}

void GAEngine::crossover(const QVector<bool> &p1, const QVector<bool> &p2,
                         QVector<bool> &c1, QVector<bool> &c2,
                         double pCross) const
{
    QRandomGenerator *rng = QRandomGenerator::global();
    const int n = p1.size();
    c1 = p1;
    c2 = p2;
    if (rng->generateDouble() >= pCross || n < 2) {
        return;
    }
    int point = rng->bounded(1, n-1);
    for (int i = point; i < n; ++i) {
        bool t = c1[i];
        c1[i] = c2[i];
        c2[i] = t;
    }
}

void GAEngine::mutate(QVector<bool> &ind, double pMut) const
{
    QRandomGenerator *rng = QRandomGenerator::global();
    const int n = ind.size();
    for (int i = 0; i < n; ++i) {
        if (rng->generateDouble() < pMut) {
            ind[i] = !ind[i];
        }
    }
}

QString GAEngine::formatTimeHHMM(int minutes) const
{
    int h = minutes / 60;
    int m = minutes % 60;
    return QString("%1:%2").arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'));
}

// Avaliação de um cromossomo
double GAEngine::evaluateChromosome(const QVector<bool> &chrom,
                                    EvalStats &stats,
                                    QVector<FlightInstance> &bestFlights,
                                    QVector<QVariantMap> &paxAssignments) const
{
    stats = EvalStats();
    bestFlights.clear();
    paxAssignments.clear();

    const int numGenes = chrom.size();
    if (numGenes != m_allFlights.size())
        return -1e9;

    // 1) construir voos ativos
    QVector<FlightInstance> flights;
    flights.reserve(numGenes);
    for (int g = 0; g < numGenes; ++g) {
        if (!chrom[g]) continue;
        const FlightTemplate &ft = m_allFlights[g];
        FlightInstance fi;
        fi.tmplId      = ft.id;
        fi.routeId     = ft.routeId;
        fi.orig        = ft.orig;
        fi.dest        = ft.dest;
        fi.depMin      = ft.depMin;
        fi.arrMin      = ft.arrMin;
        fi.aircraftIdx = -1;
        fi.capacity    = m_fleet.seatsPerAircraft;
        fi.usedSeats   = 0;
        flights.append(fi);
    }

    if (flights.isEmpty())
        return -1e9;

    stats.numFlights = flights.size();

    // 2) alocar aeronaves
    struct AircraftState {
        int airport;    // onde está (depois do primeiro voo)
        int available;  // minuto a partir do qual pode decolar
        int flightsUsed;
        bool used;
    };

    const int DAY_START  = 6*60;
    const int TURNAROUND = 60;

    QVector<AircraftState> ac;
    ac.resize(m_fleet.numAircraft);
    for (int i = 0; i < m_fleet.numAircraft; ++i) {
        ac[i].airport     = -1;       // sem base inicial
        ac[i].available   = DAY_START;
        ac[i].flightsUsed = 0;
        ac[i].used        = false;
    }

    // ordenar voos por partida
    std::sort(flights.begin(), flights.end(),
              [](const FlightInstance &a, const FlightInstance &b) {
                  return a.depMin < b.depMin;
              });

    for (FlightInstance &f : flights) {
        int bestAc   = -1;
        int bestAvail = 1e9;

        for (int i = 0; i < ac.size(); ++i) {
            if (!ac[i].used) {
                // primeira utilização define a base no voo
                if (f.depMin >= ac[i].available && ac[i].available < bestAvail) {
                    bestAc   = i;
                    bestAvail = ac[i].available;
                }
            } else {
                if (ac[i].airport != f.orig) continue;
                if (ac[i].available + TURNAROUND > f.depMin) continue;
                if (f.depMin >= ac[i].available && ac[i].available < bestAvail) {
                    bestAc   = i;
                    bestAvail = ac[i].available;
                }
            }
        }

        if (bestAc == -1)
            continue; // voo não realizado

        f.aircraftIdx        = bestAc;
        ac[bestAc].airport   = f.dest;
        ac[bestAc].available = f.arrMin;
        ac[bestAc].flightsUsed += 1;
        ac[bestAc].used = true;
    }

    // remover voos sem aeronave
    QVector<FlightInstance> flightsUsed;
    flightsUsed.reserve(flights.size());
    for (const FlightInstance &f : flights) {
        if (f.aircraftIdx >= 0)
            flightsUsed.append(f);
    }
    if (flightsUsed.isEmpty())
        return -1e9;

    flights.swap(flightsUsed);
    stats.numFlights = flights.size();

    // conta aeronaves usadas (antes do pruning final, só como base)
    {
        QSet<int> usedAc;
        for (const FlightInstance &f : flights) {
            if (f.aircraftIdx >= 0)
                usedAc.insert(f.aircraftIdx);
        }
        stats.usedAircraft = usedAc.size();
    }

    // 3) mapear voos por (orig,dest)
    const int A = m_airports.size();
    QVector<QVector<QVector<int>>> flightsByOrigDest;
    flightsByOrigDest.resize(A);
    for (int i = 0; i < A; ++i)
        flightsByOrigDest[i].resize(A);

    for (int idx = 0; idx < flights.size(); ++idx) {
        const FlightInstance &f = flights[idx];
        flightsByOrigDest[f.orig][f.dest].append(idx);
    }

    for (int i = 0; i < A; ++i)
        for (int j = 0; j < A; ++j)
            std::sort(flightsByOrigDest[i][j].begin(), flightsByOrigDest[i][j].end(),
                      [&](int a, int b) {
                          return flights[a].depMin < flights[b].depMin;
                      });

    // 4) atender demanda OD (direto ou 1 conexao)
    int totalDemand = 0;
    for (const ODDemand &d : m_od) totalDemand += d.demand;

    QVector<QVector<int>> remaining(A, QVector<int>(A, 0));
    for (const ODDemand &d : m_od)
        remaining[d.orig][d.dest] += d.demand;

    // passagens por OD, para JSON final
    for (const ODDemand &d : m_od) {
        int o        = d.orig;
        int dest     = d.dest;
        int demandLeft = remaining[o][dest];
        if (demandLeft <= 0) continue;

        struct PathCand {
            QVector<int> flightIdxs;
            int travelMin;
        };
        QVector<PathCand> candidates;

        // direto
        for (int idx : flightsByOrigDest[o][dest]) {
            const FlightInstance &f = flights[idx];
            int travel = f.arrMin - f.depMin;
            PathCand c;
            c.flightIdxs = {idx};
            c.travelMin  = travel;
            candidates.append(c);
        }

        // 1 conexao
        for (int mid = 0; mid < A; ++mid) {
            if (mid == o || mid == dest) continue;
            const auto &fo = flightsByOrigDest[o][mid];
            const auto &fd = flightsByOrigDest[mid][dest];
            if (fo.isEmpty() || fd.isEmpty()) continue;

            for (int idx1 : fo) {
                const FlightInstance &f1 = flights[idx1];
                for (int idx2 : fd) {
                    const FlightInstance &f2 = flights[idx2];
                    if (f1.arrMin + 60 > f2.depMin) continue;
                    int travel = f2.arrMin - f1.depMin;
                    PathCand c;
                    c.flightIdxs = {idx1, idx2};
                    c.travelMin  = travel;
                    candidates.append(c);
                }
            }
        }

        if (candidates.isEmpty())
            continue;

        std::sort(candidates.begin(), candidates.end(),
                  [](const PathCand &a, const PathCand &b) {
                      if (a.travelMin != b.travelMin)
                          return a.travelMin < b.travelMin;
                      return a.flightIdxs.size() < b.flightIdxs.size();
                  });

        // tenta usar vários caminhos em ordem de qualidade
        for (const PathCand &pc : candidates) {
            if (demandLeft <= 0) break;

            int pathCap = INT_MAX;
            for (int fiIdx : pc.flightIdxs) {
                const FlightInstance &f = flights[fiIdx];
                pathCap = qMin(pathCap, f.capacity - f.usedSeats);
            }
            if (pathCap <= 0) continue;

            int alloc = qMin(pathCap, demandLeft);

            for (int fiIdx : pc.flightIdxs) {
                flights[fiIdx].usedSeats += alloc;
            }

            demandLeft          -= alloc;
            stats.servedTotal   += alloc;
            if (pc.flightIdxs.size() == 1)
                stats.servedDirect += alloc;
            else
                stats.servedOneHop += alloc;

            int depFirst = flights[pc.flightIdxs.first()].depMin;
            int arrLast  = flights[pc.flightIdxs.last()].arrMin;
            int travel   = arrLast - depFirst;
            stats.totalTravelTime += 1LL * travel * alloc;

            QVariantMap rec;
            rec["orig_id"]   = o;
            rec["dest_id"]   = dest;
            rec["pax"]       = alloc;
            rec["num_legs"]  = pc.flightIdxs.size();
            rec["dep_min"]   = depFirst;
            rec["arr_min"]   = arrLast;
            rec["dep_hhmm"]  = formatTimeHHMM(depFirst);
            rec["arr_hhmm"]  = formatTimeHHMM(arrLast);

            QVariantList legs;
            for (int fiIdx : pc.flightIdxs) {
                const FlightInstance &f = flights[fiIdx];
                QVariantMap leg;
                leg["flight_index"] = fiIdx; // vamos remapear depois do pruning
                leg["route_id"]     = f.routeId;
                leg["orig_id"]      = f.orig;
                leg["dest_id"]      = f.dest;
                leg["dep_min"]      = f.depMin;
                leg["arr_min"]      = f.arrMin;
                leg["dep_hhmm"]     = formatTimeHHMM(f.depMin);
                leg["arr_hhmm"]     = formatTimeHHMM(f.arrMin);
                leg["aircraft_idx"] = f.aircraftIdx;
                legs.append(leg);
            }
            rec["legs"] = legs;

            paxAssignments.append(rec);
        }

        remaining[o][dest] = demandLeft;
    }

    int unserved = 0;
    for (int i = 0; i < A; ++i)
        for (int j = 0; j < A; ++j)
            unserved += remaining[i][j];
    stats.unserved = unserved;

    // ------------------------------------------------------------------
    // 4.5) PRUNING: remove prefixo/sufixo de voos vazios por aeronave
    // ------------------------------------------------------------------
    if (flights.isEmpty())
        return -1e9;

    QVector<QVector<int>> flightsPerAc(m_fleet.numAircraft);
    for (int idx = 0; idx < flights.size(); ++idx) {
        const FlightInstance &f = flights[idx];
        if (f.aircraftIdx >= 0 && f.aircraftIdx < m_fleet.numAircraft) {
            flightsPerAc[f.aircraftIdx].append(idx);
        }
    }

    // flights já está ordenado em depMin, então índices em flightsPerAc
    // ficam em ordem temporal.
    QVector<bool> keep(flights.size(), true);

    for (int acIdx = 0; acIdx < flightsPerAc.size(); ++acIdx) {
        const QVector<int> &idxs = flightsPerAc[acIdx];
        if (idxs.isEmpty()) continue;

        // prefixo de voos vazios
        int firstNonEmptyPos = 0;
        while (firstNonEmptyPos < idxs.size() &&
               flights[idxs[firstNonEmptyPos]].usedSeats == 0) {
            ++firstNonEmptyPos;
        }
        for (int p = 0; p < firstNonEmptyPos; ++p) {
            keep[idxs[p]] = false;
        }

        // sufixo de voos vazios
        int lastNonEmptyPos = idxs.size() - 1;
        while (lastNonEmptyPos >= 0 &&
               flights[idxs[lastNonEmptyPos]].usedSeats == 0) {
            --lastNonEmptyPos;
        }
        for (int p = lastNonEmptyPos + 1; p < idxs.size(); ++p) {
            keep[idxs[p]] = false;
        }
    }

    // monta nova lista de voos e mapeia índices antigos -> novos
    QVector<int> mapOldToNew(flights.size(), -1);
    QVector<FlightInstance> pruned;
    pruned.reserve(flights.size());
    for (int i = 0; i < flights.size(); ++i) {
        if (!keep[i]) continue;
        mapOldToNew[i] = pruned.size();
        pruned.append(flights[i]);
    }

    flights.swap(pruned);

    if (flights.isEmpty())
        return -1e9;

    stats.numFlights = flights.size();

    // remapear flight_index nos paxAssignments
    for (QVariantMap &rec : paxAssignments) {
        QVariantList legs = rec.value("legs").toList();
        for (int li = 0; li < legs.size(); ++li) {
            QVariantMap leg = legs[li].toMap();
            int oldIdx = leg.value("flight_index").toInt();
            int newIdx = (oldIdx >= 0 && oldIdx < mapOldToNew.size())
                             ? mapOldToNew[oldIdx]
                             : -1;
            leg["flight_index"] = newIdx;
            legs[li] = leg;
        }
        rec["legs"] = legs;
    }

    // recalc usedAircraft após pruning
    {
        QSet<int> usedAcAfter;
        for (const FlightInstance &f : flights) {
            if (f.aircraftIdx >= 0)
                usedAcAfter.insert(f.aircraftIdx);
        }
        stats.usedAircraft = usedAcAfter.size();
    }

    // ------------------------------------------------------------------
    // 5) calcula fitness (já com voos podados)
    // ------------------------------------------------------------------
    double score = 0.0;

    score += 100000.0 * stats.servedTotal;
    score -= 10.0 * (double)stats.totalTravelTime;

    // punir passageiros não atendidos
    score -= 50000.0 * (double)stats.unserved;

    // limitar voos e aeronaves
    if (stats.numFlights > 1000) {
        score -= 100000.0 * (stats.numFlights - 1000);
    }
    if (stats.usedAircraft > m_fleet.numAircraft) {
        score -= 100000.0 * (stats.usedAircraft - m_fleet.numAircraft);
    }

    // penalização leve pra voos vazios REMANESCENTES (no meio)
    int emptyFlights = 0;
    for (const FlightInstance &f : flights) {
        if (f.usedSeats == 0) emptyFlights++;
    }
    score -= 1000.0 * (double)emptyFlights;

    bestFlights = flights; // copia voos finais usados (já podados)
    return score;
}

GAResult GAEngine::runGA(int population, int generations,
                         ProgressCallback cbProgress)
{
    GAResult result;
    if (m_allFlights.isEmpty() || m_routes.isEmpty() || m_od.isEmpty()) {
        result.summaryText = "Missing data (routes / flights / passengers).";
        return result;
    }

    const int numGenes = m_allFlights.size();

    QVector<QVector<bool>> pop = initPopulation(population, numGenes);
    QVector<double> scores(population, -1e9);
    QJsonArray gaHistory;
    QElapsedTimer timer;

    double bestScore = -1e9;
    QVector<bool> bestInd;
    EvalStats bestStats;
    QVector<FlightInstance> bestFlights;
    QVector<QVariantMap> bestPaxAssignments;

    // população inicial
    timer.start();
    double bestGenScore = -1e9;
    double worstGenScore = 1e9;
    double sumScores = 0.0;
    EvalStats bestGenStats;

    for (int i = 0; i < population; ++i) {
        EvalStats s;
        QVector<FlightInstance> flightsTmp;
        QVector<QVariantMap> paxTmp;
        double sc = evaluateChromosome(pop[i], s, flightsTmp, paxTmp);
        scores[i] = sc;
        sumScores += sc;
        if (sc > bestGenScore) {
            bestGenScore = sc;
            bestGenStats = s;
        }
        if (sc < worstGenScore) {
            worstGenScore = sc;
        }
        if (sc > bestScore) {
            bestScore          = sc;
            bestInd            = pop[i];
            bestStats          = s;
            bestFlights        = flightsTmp;
            bestPaxAssignments = paxTmp;
        }
    }

    {
        qint64 durationMs = timer.elapsed();
        double avgScore = (population > 0) ? (sumScores / (double)population) : 0.0;
        QJsonObject genObj;
        genObj["generation"]      = 0;
        genObj["best_score"]      = bestGenScore;
        genObj["avg_score"]       = avgScore;
        genObj["worst_score"]     = worstGenScore;
        genObj["served_total"]    = bestGenStats.servedTotal;
        genObj["served_direct"]   = bestGenStats.servedDirect;
        genObj["served_1hop"]     = bestGenStats.servedOneHop;
        genObj["unserved"]        = bestGenStats.unserved;
        genObj["num_flights"]     = bestGenStats.numFlights;
        genObj["used_aircraft"]   = bestGenStats.usedAircraft;
        genObj["duration_ms"]     = (double)durationMs;
        gaHistory.append(genObj);
    }

    if (cbProgress) cbProgress(0, bestScore);

    QRandomGenerator *rng = QRandomGenerator::global();

    auto tournament = [&](const QVector<double> &scores) -> int {
        int a = rng->bounded(population);
        int b = rng->bounded(population);
        return (scores[a] > scores[b]) ? a : b;
    };

    const double pCross    = 0.8;
    const double pMut      = 0.01;
    const double eliteFrac = 0.1;
    const int eliteCount   = qMax(1, (int)(population * eliteFrac));

    for (int gen = 1; gen <= generations; ++gen) {
        QVector<QVector<bool>> newPop;
        newPop.reserve(population);

        QVector<int> idxs(population);
        for (int i = 0; i < population; ++i) idxs[i] = i;
        std::sort(idxs.begin(), idxs.end(), [&](int a, int b){
            return scores[a] > scores[b];
        });
        for (int i = 0; i < eliteCount; ++i) {
            newPop.append(pop[idxs[i]]);
        }

        while (newPop.size() < population) {
            int i1 = tournament(scores);
            int i2 = tournament(scores);
            QVector<bool> c1, c2;
            crossover(pop[i1], pop[i2], c1, c2, pCross);
            mutate(c1, pMut);
            mutate(c2, pMut);
            newPop.append(c1);
            if (newPop.size() < population)
                newPop.append(c2);
        }

        pop.swap(newPop);

        timer.restart();
        double bestGenScoreG = -1e9;
        double worstGenScoreG = 1e9;
        double sumScoresG = 0.0;
        EvalStats bestGenStatsG;

        for (int i = 0; i < population; ++i) {
            EvalStats s;
            QVector<FlightInstance> flightsTmp;
            QVector<QVariantMap> paxTmp;
            double sc = evaluateChromosome(pop[i], s, flightsTmp, paxTmp);
            scores[i] = sc;
            sumScoresG += sc;
            if (sc > bestGenScoreG) {
                bestGenScoreG = sc;
                bestGenStatsG = s;
            }
            if (sc < worstGenScoreG) {
                worstGenScoreG = sc;
            }
            if (sc > bestScore) {
                bestScore          = sc;
                bestInd            = pop[i];
                bestStats          = s;
                bestFlights        = flightsTmp;
                bestPaxAssignments = paxTmp;
            }
        }

        {
            qint64 durationMs = timer.elapsed();
            double avgScore = (population > 0) ? (sumScoresG / (double)population) : 0.0;
            QJsonObject genObj;
            genObj["generation"]      = gen;
            genObj["best_score"]      = bestGenScoreG;
            genObj["avg_score"]       = avgScore;
            genObj["worst_score"]     = worstGenScoreG;
            genObj["served_total"]    = bestGenStatsG.servedTotal;
            genObj["served_direct"]   = bestGenStatsG.servedDirect;
            genObj["served_1hop"]     = bestGenStatsG.servedOneHop;
            genObj["unserved"]        = bestGenStatsG.unserved;
            genObj["num_flights"]     = bestGenStatsG.numFlights;
            genObj["used_aircraft"]   = bestGenStatsG.usedAircraft;
            genObj["duration_ms"]     = (double)durationMs;
            gaHistory.append(genObj);
        }

        if (cbProgress) cbProgress(gen, bestScore);
    }

    // JSON de saída (voos + resumo)
    QJsonObject summary;
    summary["served_total"]          = bestStats.servedTotal;
    summary["served_direct"]         = bestStats.servedDirect;
    summary["served_1hop"]           = bestStats.servedOneHop;
    summary["unserved"]              = bestStats.unserved;
    summary["num_flights"]           = bestStats.numFlights;
    summary["used_aircraft"]         = bestStats.usedAircraft;
    summary["total_travel_time_min"] = (double)bestStats.totalTravelTime;

    QJsonArray airportsArr;
    for (const Airport &a : m_airports) {
        QJsonObject ao;
        ao["id"]   = a.id;
        ao["code"] = a.code;
        ao["name"] = a.name;
        airportsArr.append(ao);
    }

    QJsonArray flightsArr;
    for (const FlightInstance &f : bestFlights) {
        if (f.aircraftIdx < 0) continue;

        QJsonObject o;
        o["tmpl_id"]      = f.tmplId;
        o["route_id"]     = f.routeId;
        o["orig_id"]      = f.orig;
        o["dest_id"]      = f.dest;
        o["dep_min"]      = f.depMin;
        o["arr_min"]      = f.arrMin;
        o["dep_hhmm"]     = formatTimeHHMM(f.depMin);
        o["arr_hhmm"]     = formatTimeHHMM(f.arrMin);
        o["aircraft_idx"] = f.aircraftIdx;
        if (f.aircraftIdx >= 0 && f.aircraftIdx < m_fleet.aircraftIds.size())
            o["aircraft_id"] = m_fleet.aircraftIds[f.aircraftIdx];
        o["capacity"]     = f.capacity;
        o["used_seats"]   = f.usedSeats;
        flightsArr.append(o);
    }

    QJsonObject root;
    root["summary"]  = summary;
    root["airports"] = airportsArr;
    root["flights"]  = flightsArr;

    result.bestScore = bestScore;
    result.fullJson  = QJsonDocument(root);

    // JSON de histórico do GA (evolução por geração)
    QJsonObject histRoot;
    histRoot["evolution"] = gaHistory;
    result.evolutionJson  = QJsonDocument(histRoot);

    // JSON de passageiros
    QJsonArray paxArr;
    for (const QVariantMap &rec : bestPaxAssignments) {
        QJsonObject o = QJsonObject::fromVariantMap(rec);
        paxArr.append(o);
    }
    QJsonObject paxRoot;
    paxRoot["assignments"] = paxArr;
    result.passengersJson  = QJsonDocument(paxRoot);

    QString txt;
    txt += QString("Served total: %1\n").arg(bestStats.servedTotal);
    txt += QString("   direct   : %1\n").arg(bestStats.servedDirect);
    txt += QString("   1-hop    : %1\n").arg(bestStats.servedOneHop);
    txt += QString("Unserved passengers: %1\n").arg(bestStats.unserved);
    txt += QString("Flights used: %1\n").arg(bestStats.numFlights);
    txt += QString("Aircraft used: %1 / %2\n")
               .arg(bestStats.usedAircraft)
               .arg(m_fleet.numAircraft);
    txt += QString("Total travel time (min * pax): %1\n")
               .arg((qlonglong)bestStats.totalTravelTime);

    result.summaryText = txt;
    return result;
}
