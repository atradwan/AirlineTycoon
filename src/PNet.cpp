//============================================================================================
// PNet.cpp : Routinen zum verwalten der Spieler im Netzwerk
//============================================================================================
#include "AtNet.h"
#include "NetTrace.h"
#include "class.h"
#include "global.h"

#define AT_Log(...) AT_Log_I("Player", __VA_ARGS__)

extern bool bgIsLoadingSavegame;
extern SLONG gRobotSyncSlice[4];

inline bool needToSyncPlayer(const PLAYER &qPlayer, bool onlyBots) {
    if (qPlayer.IsOut != 0) {
        return false;
    }
    if (qPlayer.Owner == 0 && !onlyBots) {
        return true;
    }
    if ((Sim.bIsHost != 0) && qPlayer.Owner == 1) {
        return true;
    }
    return false;
}

//--------------------------------------------------------------------------------------------
// Returns the number of players on which this computer will send information:
//--------------------------------------------------------------------------------------------
inline SLONG NetSynchronizeGetNum(bool onlyBots) {
    SLONG n = 0;
    for (SLONG c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];
        if (needToSyncPlayer(qPlayer, onlyBots)) {
            n++;
        }
    }
    return (n);
}

//--------------------------------------------------------------------------------------------
// Sends the data concerning image to other players:
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizeImage() {
    TEAKFILE Message;
    SLONG c = 0;

    Message.Announce(1024);

    Message << ATNET_SYNC_IMAGE << NetSynchronizeGetNum(false);

    // Für den lokalen Spieler und (wenn dies der Server ist) auch für Computerspieler:
    for (c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            SLONG d = 0;

            /* The images change with every flight of the player (passengers, route image), so the
               state carries the flight it was taken after, see ATNET_SYNC_IMAGE. */
            Message << c << qPlayer.NetFlightStamp() << qPlayer.Image << qPlayer.ImageGotWorse;

            for (d = 0; d < 4; d++) {
                Message << qPlayer.Sympathie[d];
            }

            for (d = Routen.AnzEntries() - 1; d >= 0; d--) {
                Message << qPlayer.RentRouten.RentRouten[d].Image;
            }
            for (d = Cities.AnzEntries() - 1; d >= 0; d--) {
                Message << qPlayer.RentCities.RentCities[d].Image;
            }
        }
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Sends the data concerning money to other players:
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizeMoney() {
    TEAKFILE Message;
    SLONG c = 0;

    Message.Announce(256);

    Message << ATNET_SYNC_MONEY << NetSynchronizeGetNum(false);

    // Für den lokalen Spieler und (wenn dies der Server ist) auch für Computerspieler:
    for (c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            SLONG d = 0;

            /* Which of the player's flights of the day this is after, as in ATNET_SYNC_IMAGE.
               Every flight books revenue and costs, so money taken after a flight a peer has still
               to book would be booked twice there, and money taken before a flight it has already
               booked would lose that flight. */
            Message << c << qPlayer.NetFlightStamp();

            Message << qPlayer.Money << qPlayer.Credit << qPlayer.Bonus << qPlayer.AnzAktien << qPlayer.MaxAktien << qPlayer.TrustedDividende
                    << qPlayer.Dividende;

            for (d = 0; d < 4; d++) {
                Message << qPlayer.OwnsAktien[d] << qPlayer.AktienWert[d];
            }
            for (d = 0; d < 10; d++) {
                Message << qPlayer.Kurse[d];
            }
        }
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Sends the data concerning routes to other players:
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizeRoutes() {
    TEAKFILE Message;
    SLONG c = 0;

    Message.Announce(1024);

    Message << ATNET_SYNC_ROUTES << NetSynchronizeGetNum(false);

    // Für den lokalen Spieler und (wenn dies der Server ist) auch für Computerspieler:
    for (c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            /* Which of the player's flights of the day this is after (see NetFlightStamp()).
               Every flight changes a route's image, the day it was last flown and its usage, and
               a flight booked in the same minute as this message would otherwise be undone on a
               peer that has booked it already, or counted twice on one that is behind. */
            Message << c << qPlayer.NetFlightStamp();

            for (SLONG d = Routen.AnzEntries() - 1; d >= 0; d--) {
                Message << qPlayer.RentRouten.RentRouten[d].Rang << qPlayer.RentRouten.RentRouten[d].LastFlown << qPlayer.RentRouten.RentRouten[d].Image
                        << qPlayer.RentRouten.RentRouten[d].Miete << qPlayer.RentRouten.RentRouten[d].Ticketpreis
                        << qPlayer.RentRouten.RentRouten[d].TicketpreisFC << qPlayer.RentRouten.RentRouten[d].TageMitVerlust
                        << qPlayer.RentRouten.RentRouten[d].TageMitGering;
            }

            /* How well the routes are used follows from the flights and was never resent, so once
               two peers disagreed about it - one differing passenger count is enough - they never
               agreed again. Only the rented routes carry it, so send just those. */
            SLONG Rented = 0;
            for (SLONG d = 0; d < Routen.AnzEntries(); d++) {
                if (qPlayer.RentRouten.RentRouten[d].Rang != 0U) {
                    Rented++;
                }
            }
            Message << Rented;

            for (SLONG d = 0; d < Routen.AnzEntries(); d++) {
                const CRentRoute &qRoute = qPlayer.RentRouten.RentRouten[d];
                if (qRoute.Rang == 0U) {
                    continue;
                }

                Message << d << qRoute.Auslastung << qRoute.AuslastungFC << qRoute.RoutenAuslastung << qRoute.HeuteBefoerdert;
                for (SLONG e = 0; e < 7; e++) {
                    Message << qRoute.WocheBefoerdert[e];
                }
            }
        }
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Läßt einen Spieler die Ticketpreis verändern:
//--------------------------------------------------------------------------------------------
void PLAYER::NetRouteUpdateTicketpreise(SLONG RouteId, SLONG Ticketpreis, SLONG TicketpreisFC) const {
    SIM::SendSimpleMessage(ATNET_SYNCROUTECHANGE, 0, PlayerNum, RouteId, Ticketpreis, TicketpreisFC);
}

//--------------------------------------------------------------------------------------------
// Sends the data concerning flags to other players:
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizeFlags() {
    TEAKFILE Message;
    SLONG c = 0;

    Message.Announce(64);

    Message << ATNET_SYNC_FLAGS << NetSynchronizeGetNum(false);

    // Für den lokalen Spieler und (wenn dies der Server ist) auch für Computerspieler:
    for (c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            Message << c;

            Message << qPlayer.SickTokay << qPlayer.RunningToToilet << qPlayer.PlayerSmoking << qPlayer.Stunned << qPlayer.OfficeState << qPlayer.Koffein
                    << qPlayer.NumFlights << qPlayer.WalkSpeed << qPlayer.WerbeBroschuere << qPlayer.TelephoneDown << qPlayer.Presseerklaerung
                    << qPlayer.SecurityFlags << qPlayer.PlayerStinking << qPlayer.RocketFlags << qPlayer.LastRocketFlags << qPlayer.LaptopVirus;
            /* Whether the player flies routes: a bot decides this in its office, on the host only,
               but every peer's night (RobotPlanRoutes) plans and prices the routes from it. */
            Message << qPlayer.DoRoutes << qPlayer.WantToDoRoutes;
        }
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizeItems() {
    TEAKFILE Message;
    SLONG c = 0;

    Message.Announce(64);

    Message << ATNET_SYNC_ITEMS << NetSynchronizeGetNum(false);

    // Für den lokalen Spieler und (wenn dies der Server ist) auch für Computerspieler:
    for (c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            Message << c;

            for (SLONG d = 0; d < 6; d++) {
                Message << qPlayer.Items[d];
            }
        }
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizePlanes() {
    TEAKFILE Message;
    SLONG c = 0;

    Message.Announce(1024);

    /* This used to be sent by the host for the computer players only. A human player's planes
       reached the others only through the messages for single changes (buying, flight plans,
       ...), so a fleet that drifted apart once stayed apart for the rest of the game, also in
       every savegame. The flights booked from it then differed, the owner's money was never
       taken again (see ATNET_SYNC_MONEY) and a peer could see the player go bankrupt while the
       owner did not. Every peer now sends its own human player as well, once a day. */
    Message << ATNET_SYNC_PLANES << NetSynchronizeGetNum(false);

    // Für den lokalen Spieler und (wenn dies der Server ist) auch für Computerspieler:
    for (c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            Message << c;
            Message << qPlayer.Planes << qPlayer.Auftraege << qPlayer.Frachten << qPlayer.RentCities;
        }
    }

    /* One file per day, to compare against what a receiver could not read. */
    NetDumpMessage("sent", Message.MemBuffer, static_cast<SLONG>(Message.MemBufferUsed), true);
    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizeMeeting() {
    TEAKFILE Message;
    SLONG c = 0;

    Message.Announce(64);

    Message << ATNET_SYNC_MEETING << NetSynchronizeGetNum(false);

    // Wenn dies der Server ist für alle Computerspieler:
    for (c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            Message << c;
            Message << qPlayer.ArabTrust << qPlayer.ArabMode << qPlayer.ArabMode2 << qPlayer.ArabMode3 << qPlayer.ArabActive;
            Message << qPlayer.ArabOpfer << qPlayer.ArabOpfer2 << qPlayer.ArabOpfer3 << qPlayer.ArabPlane << qPlayer.ArabHints;
            Message << qPlayer.ArabTimeout << qPlayer.NumPassengers << qPlayer.NumFracht;
        }
    }

    Message << Sim.bIsHost;
    if (Sim.bIsHost != 0) {
        Message << Sim.SabotageActs;
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
//
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizeSabotage() const {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_SABOTAGE_ARAB << PlayerNum;

    Message << ArabOpfer << ArabMode << ArabActive << ArabPlane << ArabOpfer2 << ArabMode2 << ArabOpfer3 << ArabMode3 << ArabTimeout;

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Transfers a flightplan
//--------------------------------------------------------------------------------------------
void PLAYER::NetUpdateFlightplan(SLONG PlaneId) {
    TEAKFILE Message;

    Message.Announce(1024);

    Message << ATNET_FP_UPDATE;
    Message << PlaneId << PlayerNum;
    Message << Planes[PlaneId].Flugplan;
    Message << Sim.Date << Sim.GetHour(); // when the plan was made, see ATNET_FP_UPDATE

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Player took an order flight ==> Tell the others:
// Type: 1 - LastMinute
// Type: 2 - Reisebüro
// Type: 3 - Fracht
// Type: 4 - Ausland, City = CityIndex
// This function only handle the blackboard in the room; It does not update the player's data
//--------------------------------------------------------------------------------------------
void PLAYER::NetUpdateTook(SLONG Type, SLONG Index, SLONG City) const {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_PLAYER_TOOK << PlayerNum << Type << Index << City << NetJobsRefillEpoch(); // which refill the boards were at

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Let's the player take the order on other computers, too:
//--------------------------------------------------------------------------------------------
void PLAYER::NetUpdateOrder(const CAuftrag &auftrag) const {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_TAKE_ORDER;
    Message << PlayerNum << auftrag;

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Let's the player take the order on other computers, too:
//--------------------------------------------------------------------------------------------
void PLAYER::NetUpdateFreightOrder(const CFracht &auftrag) const {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_TAKE_FREIGHT;
    Message << PlayerNum << auftrag;

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Updates the rental data of one route and back:
//--------------------------------------------------------------------------------------------
void PLAYER::NetUpdateRentRoute(SLONG Route1Id, SLONG Route2Id) {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_TAKE_ROUTE;

    Message << PlayerNum << Route1Id << Route2Id;
    Message << RentRouten.RentRouten[Route1Id];
    Message << RentRouten.RentRouten[Route2Id];

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Updates the Kooperation status:
//--------------------------------------------------------------------------------------------
void PLAYER::NetSynchronizeKooperation() const {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_DIALOG_KOOP;

    Message << PlayerNum << Kooperation;

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Sends what this peer's people earn and how they feel:
//--------------------------------------------------------------------------------------------
/* Salaries and happiness are changed by every peer alike - at night, and when a salary change or
   the end of a strike is announced - so they should never drift. But nothing repaired them when
   they did: a strike that started an hour apart once left a whole staff 10 happiness apart for
   the rest of the game, which then decides who quits and when the next strike comes. The owner
   resends them every hour, as it does money, image and routes. */
void PLAYER::NetSynchronizeStaff() {
    TEAKFILE Message;

    Message.Announce(1024);

    Message << ATNET_SYNC_STAFF << NetSynchronizeGetNum(false);

    for (SLONG c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            SLONG Anz = 0;
            for (SLONG d = 0; d < Workers.Workers.AnzEntries(); d++) {
                if (Workers.Workers[d].Employer == c) {
                    Anz++;
                }
            }

            Message << c << Anz;

            for (SLONG d = 0; d < Workers.Workers.AnzEntries(); d++) {
                if (Workers.Workers[d].Employer == c) {
                    Message << d << Workers.Workers[d].Gehalt << Workers.Workers[d].Happyness;
                }
            }
        }
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Changes how much this player likes another one, and tells the other peers:
//--------------------------------------------------------------------------------------------
/* How a player feels about the others belongs to that player, so a dialog may only change it
   where it happens - and the owner's next ATNET_SYNC_IMAGE then overwrote it. On a client,
   being nice to a bot therefore had no effect at all, while the host's human was heard.
   ATNET_ADD_SYMPATHIE exists for this and had no sender. */
void PLAYER::NetAddSympathie(SLONG Target, SLONG Delta) {
    if (Target < 0 || Target >= 4) {
        return;
    }

    Sympathie[Target] += Delta;
    Limit(static_cast<SLONG>(-1000), Sympathie[Target], static_cast<SLONG>(1000));

    if (Sim.bNetwork != 0) {
        SIM::SendSimpleMessage(ATNET_ADD_SYMPATHIE, 0, PlayerNum, Target, Delta);
    }
}

//--------------------------------------------------------------------------------------------
// Updates the total number of workers and which planes they work on:
//--------------------------------------------------------------------------------------------
void PLAYER::NetUpdateWorkers() {
    TEAKFILE Message;
    SLONG m = 0;
    SLONG n = 0;
    SLONG c = 0;

    if (bgIsLoadingSavegame) {
        return;
    }

    /* Callers rely on this side effect whether or not anything gets sent - also in single
       player - so it stays ahead of the ownership check. */
    UpdateStatistics();

    if (!NetIsAuthoritative()) {
        return;
    }

    Message.Announce(128);

    Message << ATNET_PERSONNEL;

    m = static_cast<SLONG>(Statistiken[STAT_ZUFR_PERSONAL].GetAtPastDay(0));
    n = static_cast<SLONG>(Statistiken[STAT_MITARBEITER].GetAtPastDay(0));

    Message << PlayerNum << m << n;

    for (c = 0; c < Planes.AnzEntries(); c++) {
        if (Planes.IsInAlbum(c) != 0) {
            Message << c;
            Message << Planes[c].AnzPiloten;
            Message << Planes[c].AnzBegleiter;
            Message << Planes[c].PersonalQuality;
        }
    }

    c = -1;
    Message << c;

    SIM::SendMemFile(Message);

    if (Owner == 0 || ((Owner == 1) && (Sim.bIsHost != 0) && !RobotUse(ROBOT_USE_FAKE_PERSONAL))) {
        /*
        SLONG c = 0;
        SLONG d = 0;

        for (c = d = 0; c < Workers.Workers.AnzEntries(); c++) {
            if (Workers.Workers[c].Employer == PlayerNum) {
                d += Workers.Workers[c].Gehalt;
            }
        }
        Statistiken[STAT_GEHALT].SetAtPastDay(-d);
        */

        SIM::SendSimpleMessage(ATNET_SYNCGEHALT, 0, PlayerNum, Statistiken[STAT_GEHALT].GetAtPastDay(0));
    }
}

//--------------------------------------------------------------------------------------------
// Organizes saving in the network:
//--------------------------------------------------------------------------------------------
void PLAYER::NetSave(DWORD UniqueGameId, SLONG CursorY, const CString &Name) {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_IO_SAVE;

    Message << UniqueGameId << CursorY << Name;

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Broadcasts a plane's properties:
//--------------------------------------------------------------------------------------------
/* Only the peer that owns a player may broadcast its state: its own human's machine, or the
   host for the bots. MapWorkers() broadcasts plane properties and staff for all of a player's
   planes, and it also runs when a peer merely applies a hire it received from the network - so
   without this check a client echoed a bot's plane settings back to the host from its own,
   possibly older copy, and could revert a refit the bot had just ordered. */
bool PLAYER::NetIsAuthoritative() const { return (Sim.bNetwork != 0) && (Owner == 0 || (Owner == 1 && Sim.bIsHost != 0)); }

void PLAYER::NetUpdatePlaneProps(SLONG PlaneId) {
    TEAKFILE Message;

    Message.Announce(128);

    if (bgIsLoadingSavegame || !NetIsAuthoritative()) {
        return;
    }

    Message << ATNET_PLANEPROPS;

    Message << PlayerNum << PlaneId;
    Message << MechMode;

    if (PlaneId != -1) {
        CPlane &qPlane = Planes[PlaneId];

        Message << qPlane.Sitze << qPlane.SitzeTarget << qPlane.Essen << qPlane.EssenTarget << qPlane.Tabletts << qPlane.TablettsTarget << qPlane.Deco
                << qPlane.DecoTarget << qPlane.Triebwerk << qPlane.TriebwerkTarget << qPlane.Reifen << qPlane.ReifenTarget << qPlane.Elektronik
                << qPlane.ElektronikTarget << qPlane.Sicherheit << qPlane.SicherheitTarget << qPlane.MaxPassagiereTarget << qPlane.MaxPassagiereTargetFC;

        Message << qPlane.WorstZustand << qPlane.Zustand << qPlane.TargetZustand;
        Message << qPlane.AnzBegleiter << qPlane.MaxBegleiter;
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Resends the kerosine state of this peer's players:
//--------------------------------------------------------------------------------------------
/* The tank drains with every flight, and every peer works that out for itself; the state is only
   broadcast when somebody buys a tank, buys kerosine or opens it. So a single flight that went
   differently for a moment left the tanks apart for the rest of the game - and what is in the
   tank decides what the next flight costs. The owner resends it every hour, as it does money,
   image, routes and staff. */
void PLAYER::NetSynchronizeKerosin() {
    for (SLONG c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];

        if (needToSyncPlayer(qPlayer, false)) {
            qPlayer.NetUpdateKerosin();
        }
    }
}

//--------------------------------------------------------------------------------------------
// Host only: broadcasts how many people want to fly each route (Routen[].Bedarf).
//--------------------------------------------------------------------------------------------
/* Every airline's flights take their passengers from this, but it was never sent: each peer
   lowers it for the flights it books and blends it once a night. In a network save every peer
   writes its own file when the save message reaches it, a few game minutes apart, so each
   load started the peers off with different demand - and the nightly blend keeps 6/7 of any
   difference, so it never went away. The host's copy wins. */
void PLAYER::NetSynchronizeDemand() {
    if (Sim.bNetwork == 0 || Sim.bIsHost == 0) {
        return;
    }

    TEAKFILE Message;
    const SLONG Anz = Routen.AnzEntries();

    Message.Announce(64 + Anz * 4);

    Message << ATNET_SYNC_DEMAND << Sim.Date << Anz;
    for (SLONG d = 0; d < Anz; d++) {
        Message << ((Routen.IsInAlbum(d) != 0) ? Routen[d].Bedarf : SLONG(0));
    }

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// After a network load: every peer sends what it owns (the host also its bots).
//--------------------------------------------------------------------------------------------
/* Each peer loads its own file, written when the save message reached it, so the copies of the
   others' planes and flags differ from the start. Without this they stayed apart until the
   night's sync - a whole day of other flights, money and route usage, during which the stamp
   checks then refused the owner's money and route figures. */
void PLAYER::NetSynchronizeLoadedPlayers() {
    if (Sim.bNetwork == 0) {
        return;
    }
    NetTraceEvent("RESYNC after load");

    /* First the owner's count of today's flights: the files were saved minutes apart, so the
       counts differ, and the money and route figures below would be skipped as "not the same
       flights" (NetTakeOwnerState) - for a bot until the next briefing. */
    TEAKFILE Message;
    Message.Announce(64);
    SLONG Anz = 0;
    for (SLONG c = 0; c < 4; c++) {
        Anz += static_cast<SLONG>(needToSyncPlayer(Sim.Players.Players[c], false));
    }
    Message << ATNET_SYNC_STAMPS << Anz;
    for (SLONG c = 0; c < 4; c++) {
        PLAYER &qPlayer = Sim.Players.Players[c];
        if (needToSyncPlayer(qPlayer, false)) {
            Message << c << qPlayer.NetFlightsDate << qPlayer.NetFlightsToday;
        }
    }
    SIM::SendMemFile(Message);

    NetSynchronizePlanes();
    NetSynchronizeFlags();
    NetSynchronizeRoutes();
    NetSynchronizeMoney();
    NetSynchronizeImage();
    NetSynchronizeItems();
    NetSynchronizeDemand();
}

//--------------------------------------------------------------------------------------------
// Broadcasts a players kerosine state:
//--------------------------------------------------------------------------------------------
void PLAYER::NetUpdateKerosin() const {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_SYNCKEROSIN;
    Message << PlayerNum << Tank << TankOpen << TankInhalt << KerosinQuali << KerosinKind << TankPreis << NetFlightStamp();

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// How many flights of this player were booked today; the same on every peer at the same point.
// Sent with every state that a flight changes (money, image, routes, tank), so that a peer only
// takes a state that was taken after the same flights as its own.
//--------------------------------------------------------------------------------------------
SLONG PLAYER::NetFlightStamp() const {
    if (NetFlightsDate != Sim.Date) {
        return Sim.Date * 100000;
    }
    return Sim.Date * 100000 + NetFlightsToday;
}

//--------------------------------------------------------------------------------------------
// A flight of this player was booked (see NetFlightStamp())
//--------------------------------------------------------------------------------------------
void PLAYER::NetFlightBooked() {
    if (NetFlightsDate != Sim.Date) {
        NetFlightsDate = Sim.Date;
        NetFlightsToday = 0;
    }
    NetFlightsToday++;
}

//--------------------------------------------------------------------------------------------
// The owner sent its tank state. The owner may be ahead of us or behind: a state taken after a
// flight we have still to book would be emptied by that flight a second time, and a state taken
// before a flight we have already booked would bring back what it took.
//--------------------------------------------------------------------------------------------
void PLAYER::NetReceiveKerosin(const NetTankState &State) {
    const SLONG Mine = NetFlightStamp();

    if (State.Stamp == Mine) {
        NetTankPending = State;
        NetApplyPendingKerosin();
    } else if (State.Stamp > Mine) {
        // The owner has booked flights we have still to book: apply it once we have booked them
        NetTankPending = State;
    } else {
        // We have already booked a flight the owner had not: wait for its next state
        NetTraceEvent("SKIP what=kerosin player=%ld theirs=%ld mine=%ld", static_cast<long>(PlayerNum), static_cast<long>(State.Stamp),
                      static_cast<long>(Mine));
    }
}

void PLAYER::NetApplyPendingKerosin() {
    if (NetTankPending.Stamp == -1) {
        return;
    }

    const SLONG Mine = NetFlightStamp();
    if (NetTankPending.Stamp < Mine) {
        NetTraceEvent("SKIP what=kerosin player=%ld theirs=%ld mine=%ld", static_cast<long>(PlayerNum), static_cast<long>(NetTankPending.Stamp),
                      static_cast<long>(Mine));
        NetTankPending.Stamp = -1;
    } else if (NetTankPending.Stamp == Mine) {
        Tank = NetTankPending.Tank;
        TankOpen = NetTankPending.TankOpen;
        TankInhalt = NetTankPending.TankInhalt;
        KerosinQuali = NetTankPending.KerosinQuali;
        KerosinKind = NetTankPending.KerosinKind;
        TankPreis = NetTankPending.TankPreis;
        NetTankPending.Stamp = -1;
    }
}

//--------------------------------------------------------------------------------------------
// Broadcasts a xplane buy:
//--------------------------------------------------------------------------------------------
void PLAYER::NetBuyXPlane(SLONG Anzahl, CXPlane &plane) const {
    TEAKFILE Message;

    Message.Announce(128);

    Message << ATNET_BUY_NEWX;
    Message << PlayerNum << Anzahl << plane;

    SIM::SendMemFile(Message);
}

//--------------------------------------------------------------------------------------------
// Broadcasts robot state:
//--------------------------------------------------------------------------------------------
void PLAYER::NetSyncRobot(SLONG Par1, SLONG Par2) const {
    // AT_Log("%s NetSyncRobot(): Par1 = %d, Par2 = %d", AirlineX.c_str(), Par1, Par2);

    TEAKFILE Message;

    Message.Announce(128);
    Message << ATNET_ROBOT_EXECUTE << PlayerNum << Par1 << Par2;

    for (SLONG c = 0; c < 4; c++) {
        Message << Sympathie[c];
    }
    for (SLONG c = 0; c < RobotActions.AnzEntries(); c++) {
        Message << RobotActions[c];
    }

    SIM::SendMemFile(Message);
    gRobotSyncSlice[PlayerNum] = Sim.TimeSlice;
}
