// Copyright InviMind. All Rights Reserved.

#include "Parsing/FOSMPBFParser.h"
#include "OSMWorldGenCore.h"

#if OSM_WITH_LIBOSMIUM
#include <osmium/io/any_input.hpp>
#include <osmium/handler.hpp>
#include <osmium/visitor.hpp>
#endif

bool FOSMPBFParser::Parse(
    const FString& FilePath,
    FOSMParseResult& OutResult,
    TFunction<void(float Percent, const FText& Status)> OnProgress,
    FThreadSafeBool* bCancel)
{
#if OSM_WITH_LIBOSMIUM
    const double StartTime = FPlatformTime::Seconds();
    OutResult.Reset();

    if (!FPaths::FileExists(FilePath))
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("PBF Parse error: File does not exist '%s'"), *FilePath);
        return false;
    }

    try
    {
        std::string StandardFilePath = TCHAR_TO_UTF8(*FilePath);
        osmium::io::File InputFile{StandardFilePath};
        osmium::io::Reader Reader{InputFile};

        if (OnProgress)
        {
            OnProgress(0.10f, NSLOCTEXT("OSM", "ParsingPBF", "Streaming PBF data..."));
        }

        // Internal handler class mapping osmium nodes/ways/relations
        struct FOsmiumVisitor : public osmium::handler::Handler
        {
            FOSMParseResult* Result;
            FThreadSafeBool* bCancel;

            void node(const osmium::Node& node)
            {
                if (bCancel && *bCancel) return;
                FOSMNode ParsedNode(node.id(), node.location().lat(), node.location().lon());
                for (const auto& tag : node.tags())
                {
                    ParsedNode.Tags.Add(UTF8_TO_TCHAR(tag.key()), UTF8_TO_TCHAR(tag.value()));
                }
                Result->Nodes.Add(node.id(), MoveTemp(ParsedNode));
                Result->TotalElementsProcessed++;
            }

            void way(const osmium::Way& way)
            {
                if (bCancel && *bCancel) return;
                FOSMWay ParsedWay;
                ParsedWay.Id = way.id();
                for (const auto& nr : way.nodes())
                {
                    ParsedWay.NodeRefs.Add(nr.ref());
                }
                ParsedWay.bIsClosed = way.is_closed();
                for (const auto& tag : way.tags())
                {
                    ParsedWay.Tags.Add(UTF8_TO_TCHAR(tag.key()), UTF8_TO_TCHAR(tag.value()));
                }
                Result->Ways.Add(way.id(), MoveTemp(ParsedWay));
                Result->TotalElementsProcessed++;
            }

            void relation(const osmium::Relation& relation)
            {
                if (bCancel && *bCancel) return;
                FOSMRelation ParsedRelation;
                ParsedRelation.Id = relation.id();
                for (const auto& member : relation.members())
                {
                    FOSMRelationMember m;
                    m.Ref = member.ref();
                    m.Role = UTF8_TO_TCHAR(member.role());
                    if (member.type() == osmium::item_type::node) m.Type = EOSMRelationMemberType::Node;
                    else if (member.type() == osmium::item_type::way) m.Type = EOSMRelationMemberType::Way;
                    else if (member.type() == osmium::item_type::relation) m.Type = EOSMRelationMemberType::Relation;
                    ParsedRelation.Members.Add(MoveTemp(m));
                }
                for (const auto& tag : relation.tags())
                {
                    ParsedRelation.Tags.Add(UTF8_TO_TCHAR(tag.key()), UTF8_TO_TCHAR(tag.value()));
                }
                ParsedRelation.ResolveType();
                Result->Relations.Add(relation.id(), MoveTemp(ParsedRelation));
                Result->TotalElementsProcessed++;
            }
        };

        FOsmiumVisitor Visitor;
        Visitor.Result = &OutResult;
        Visitor.bCancel = bCancel;

        osmium::apply(Reader, Visitor);
        Reader.close();

        if (bCancel && *bCancel)
        {
            return false;
        }

        if (OnProgress)
        {
            OnProgress(0.90f, NSLOCTEXT("OSM", "ResolvingPBFCoordinates", "Resolving PBF way coordinates..."));
        }

        OutResult.ResolveWayCoordinates();
        OutResult.ParseTimeSeconds = static_cast<float>(FPlatformTime::Seconds() - StartTime);

        if (OnProgress)
        {
            OnProgress(1.0f, NSLOCTEXT("OSM", "PBFParseComplete", "PBF Parse Complete!"));
        }

        UE_LOG(LogOSMWorldGen, Log, TEXT("PBF Parse succeeded in %.2fs. Nodes: %d, Ways: %d, Relations: %d"),
            OutResult.ParseTimeSeconds, OutResult.Nodes.Num(), OutResult.Ways.Num(), OutResult.Relations.Num());

        return true;
    }
    catch (const std::exception& Ex)
    {
        UE_LOG(LogOSMWorldGen, Error, TEXT("PBF Exception: %s"), UTF8_TO_TCHAR(Ex.what()));
        return false;
    }
#else
    UE_LOG(LogOSMWorldGen, Error, TEXT("PBF parsing is disabled because libosmium was not compiled in (OSM_WITH_LIBOSMIUM=0)."));
    return false;
#endif
}
