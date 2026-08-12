// Copyright InviMind. All Rights Reserved.

#include "Parsing/FOSMXMLParser.h"
#include "OSMWorldGenCore.h"
#include "Misc/FileHelper.h"
#include "XmlFile.h"

bool FOSMXMLParser::Parse(
    const FString& FilePath,
    FOSMParseResult& OutResult,
    TFunction<void(float Percent, const FText& Status)> OnProgress,
    FThreadSafeBool* bCancel)
{
    const double StartTime = FPlatformTime::Seconds();
    OutResult.Reset();

    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("XML Parse error: File does not exist '%s'"), *FilePath);
        return false;
    }

    if (OnProgress)
    {
        OnProgress(0.05f, NSLOCTEXT("OSM", "ReadingXML", "Loading XML File..."));
    }

    FString FileContent;
    if (!FFileHelper::LoadFileToString(FileContent, *FilePath))
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("XML Parse error: Failed to read file content '%s'"), *FilePath);
        return false;
    }

    if (bCancel && *bCancel)
    {
        return false;
    }

    if (OnProgress)
    {
        OnProgress(0.20f, NSLOCTEXT("OSM", "ParsingXML", "Parsing XML structure..."));
    }

    FXmlFile XmlFile;
    if (!XmlFile.LoadFile(FileContent, EConstructMethod::ConstructFromBuffer))
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("XML Parse error: Malformed XML in '%s'"), *FilePath);
        return false;
    }

    const FXmlNode* RootNode = XmlFile.GetRootNode();
    if (!RootNode || RootNode->GetTag() != TEXT("osm"))
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("XML Parse error: Root element is not <osm> in '%s'"), *FilePath);
        return false;
    }

    const TArray<FXmlNode*>& Children = RootNode->GetChildrenNodes();
    const int32 TotalChildren = Children.Num();

    for (int32 Index = 0; Index < TotalChildren; ++Index)
    {
        if (bCancel && *bCancel)
        {
            return false;
        }

        if (OnProgress && (Index % 5000 == 0 || Index == TotalChildren - 1))
        {
            const float Progress = 0.20f + 0.70f * (static_cast<float>(Index) / static_cast<float>(FMath::Max(1, TotalChildren)));
            OnProgress(Progress, FText::Format(NSLOCTEXT("OSM", "ProcessingXMLElements", "Processing XML element {0} of {1}..."), Index + 1, TotalChildren));
        }

        const FXmlNode* Child = Children[Index];
        const FString& TagName = Child->GetTag();

        if (TagName == TEXT("node"))
        {
            const int64 Id = FCString::Atoi64(*Child->GetAttribute(TEXT("id")));
            const double Lat = FCString::Atof(*Child->GetAttribute(TEXT("lat")));
            const double Lon = FCString::Atof(*Child->GetAttribute(TEXT("lon")));

            FOSMNode Node(Id, Lat, Lon);
            for (const FXmlNode* TagChild : Child->GetChildrenNodes())
            {
                if (TagChild->GetTag() == TEXT("tag"))
                {
                    Node.Tags.Add(TagChild->GetAttribute(TEXT("k")), TagChild->GetAttribute(TEXT("v")));
                }
            }
            OutResult.Nodes.Add(Id, MoveTemp(Node));
            OutResult.TotalElementsProcessed++;
        }
        else if (TagName == TEXT("way"))
        {
            const int64 Id = FCString::Atoi64(*Child->GetAttribute(TEXT("id")));
            FOSMWay Way;
            Way.Id = Id;

            for (const FXmlNode* WayChild : Child->GetChildrenNodes())
            {
                const FString& ChildTag = WayChild->GetTag();
                if (ChildTag == TEXT("nd"))
                {
                    const int64 Ref = FCString::Atoi64(*WayChild->GetAttribute(TEXT("ref")));
                    Way.NodeRefs.Add(Ref);
                }
                else if (ChildTag == TEXT("tag"))
                {
                    Way.Tags.Add(WayChild->GetAttribute(TEXT("k")), WayChild->GetAttribute(TEXT("v")));
                }
            }

            if (Way.NodeRefs.Num() >= 2)
            {
                Way.bIsClosed = (Way.NodeRefs[0] == Way.NodeRefs.Last());
            }
            OutResult.Ways.Add(Id, MoveTemp(Way));
            OutResult.TotalElementsProcessed++;
        }
        else if (TagName == TEXT("relation"))
        {
            const int64 Id = FCString::Atoi64(*Child->GetAttribute(TEXT("id")));
            FOSMRelation Relation;
            Relation.Id = Id;

            for (const FXmlNode* RelChild : Child->GetChildrenNodes())
            {
                const FString& ChildTag = RelChild->GetTag();
                if (ChildTag == TEXT("member"))
                {
                    FOSMRelationMember Member;
                    const FString& TypeStr = RelChild->GetAttribute(TEXT("type"));
                    if (TypeStr == TEXT("node")) Member.Type = EOSMRelationMemberType::Node;
                    else if (TypeStr == TEXT("way")) Member.Type = EOSMRelationMemberType::Way;
                    else if (TypeStr == TEXT("relation")) Member.Type = EOSMRelationMemberType::Relation;

                    Member.Ref = FCString::Atoi64(*RelChild->GetAttribute(TEXT("ref")));
                    Member.Role = RelChild->GetAttribute(TEXT("role"));
                    Relation.Members.Add(MoveTemp(Member));
                }
                else if (ChildTag == TEXT("tag"))
                {
                    Relation.Tags.Add(RelChild->GetAttribute(TEXT("k")), RelChild->GetAttribute(TEXT("v")));
                }
            }
            Relation.ResolveType();
            OutResult.Relations.Add(Id, MoveTemp(Relation));
            OutResult.TotalElementsProcessed++;
        }
    }

    if (OnProgress)
    {
        OnProgress(0.95f, NSLOCTEXT("OSM", "ResolvingWayCoordinates", "Resolving way coordinates..."));
    }

    OutResult.ResolveWayCoordinates();
    OutResult.ParseTimeSeconds = static_cast<float>(FPlatformTime::Seconds() - StartTime);

    if (OnProgress)
    {
        OnProgress(1.0f, NSLOCTEXT("OSM", "XMLParseComplete", "XML Parse Complete!"));
    }

    UE_LOG(LogOSMWorldGen, Log, TEXT("XML Parse succeeded in %.2fs. Nodes: %d, Ways: %d, Relations: %d"),
        OutResult.ParseTimeSeconds, OutResult.Nodes.Num(), OutResult.Ways.Num(), OutResult.Relations.Num());

    return true;
}
