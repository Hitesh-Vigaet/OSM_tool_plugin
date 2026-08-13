// Copyright InviMind. All Rights Reserved.

#include "Session/FOSMGraphSession.h"
#include "Graph/UOSMCityGraph.h"

FOSMGraphSession& FOSMGraphSession::Get()
{
    // Function-local static: constructed on first use, after the module and the UObject system
    // are up. A file-scope static would run before either and hold a TStrongObjectPtr across
    // engine initialisation.
    static FOSMGraphSession Session;
    return Session;
}

void FOSMGraphSession::Set(UOSMCityGraph* InGraph, const FOSMRegion& InRegion, const FOSMGraphReport& InReport)
{
    Graph.Reset(InGraph);
    Region = InRegion;
    Report = InReport;
}

void FOSMGraphSession::Clear()
{
    Graph.Reset();
    Region = FOSMRegion();
    Report = FOSMGraphReport();
}
