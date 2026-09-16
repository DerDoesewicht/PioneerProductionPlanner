#include "SFPResourceNodeInventory.h"

#include "Buildables/FGBuildableResourceExtractor.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Resources/FGResourceNode.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr double MaximumExtractorNodeDistanceCm = 1600.0;

	UObject* ReadObjectProperty(const UObject* Object, const TArray<FName>& Names)
	{
		if (!IsValid(Object)) return nullptr;
		for (const FName Name : Names)
		{
			const FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(
				Object->GetClass()->FindPropertyByName(Name));
			if (Property == nullptr) continue;
			if (UObject* Value = Property->GetObjectPropertyValue_InContainer(Object); IsValid(Value))
			{
				return Value;
			}
		}
		return nullptr;
	}

	UClass* ReadResourceClass(const AFGResourceNode* Node)
	{
		return Cast<UClass>(ReadObjectProperty(Node, {
			TEXT("mResourceClass"), TEXT("ResourceClass"), TEXT("mResourceDescriptor")
		}));
	}

	int32 ReadPurityIndex(const AFGResourceNode* Node)
	{
		if (!IsValid(Node)) return INDEX_NONE;
		const FProperty* Property = Node->GetClass()->FindPropertyByName(TEXT("mPurity"));
		if (Property == nullptr) return INDEX_NONE;

		FString Value;
		Property->ExportTextItem_Direct(
			Value,
			Property->ContainerPtrToValuePtr<void>(Node),
			nullptr,
			const_cast<AFGResourceNode*>(Node),
			PPF_None);
		if (Value.Contains(TEXT("Impure"), ESearchCase::IgnoreCase)
			|| Value.Contains(TEXT("Unrein"), ESearchCase::IgnoreCase)) return 0;
		if (Value.Contains(TEXT("Normal"), ESearchCase::IgnoreCase)) return 1;
		if (Value.Contains(TEXT("Pure"), ESearchCase::IgnoreCase)
			|| Value.Contains(TEXT("Rein"), ESearchCase::IgnoreCase)) return 2;

		if (const FNumericProperty* Numeric = CastField<FNumericProperty>(Property))
		{
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
			const int64 Raw = Numeric->GetSignedIntPropertyValue(ValuePtr);
			return Raw >= 0 && Raw <= 2 ? static_cast<int32>(Raw) : INDEX_NONE;
		}
		if (const FEnumProperty* Enum = CastField<FEnumProperty>(Property))
		{
			const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Node);
			const int64 Raw = Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(ValuePtr);
			return Raw >= 0 && Raw <= 2 ? static_cast<int32>(Raw) : INDEX_NONE;
		}
		return INDEX_NONE;
	}

	bool IsExtractorCandidate(const AActor* Actor)
	{
		if (!IsValid(Actor)) return false;
		const FString ClassPath = Actor->GetClass()->GetPathName();
		if (ClassPath.Contains(TEXT("WaterExtractor"), ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TEXT("WaterPump"), ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TEXT("PortableMiner"), ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TEXT("Fracking"), ESearchCase::IgnoreCase))
		{
			return false;
		}
		return Actor->IsA(AFGBuildableResourceExtractor::StaticClass())
			|| ClassPath.Contains(TEXT("Miner"), ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TEXT("OilPump"), ESearchCase::IgnoreCase)
			|| ClassPath.Contains(TEXT("OilExtractor"), ESearchCase::IgnoreCase);
	}

	AFGResourceNode* ReadLinkedNode(const AActor* Extractor)
	{
		return Cast<AFGResourceNode>(ReadObjectProperty(Extractor, {
			TEXT("mExtractableResource"), TEXT("mResourceNode"), TEXT("mExtractableResourceNode"),
			TEXT("ExtractableResource"), TEXT("ResourceNode")
		}));
	}

	void IncrementTotal(FSFPResourceNodeAvailability& Availability, const int32 Purity)
	{
		if (Purity == 0) ++Availability.ImpureTotal;
		else if (Purity == 1) ++Availability.NormalTotal;
		else if (Purity == 2) ++Availability.PureTotal;
	}

	void IncrementOccupied(FSFPResourceNodeAvailability& Availability, const int32 Purity)
	{
		if (Purity == 0) ++Availability.ImpureOccupied;
		else if (Purity == 1) ++Availability.NormalOccupied;
		else if (Purity == 2) ++Availability.PureOccupied;
	}
}

int32 FSFPResourceNodeAvailability::TotalForPurity(const FString& Purity) const
{
	if (Purity.Equals(TEXT("Rein"), ESearchCase::IgnoreCase)) return PureTotal;
	if (Purity.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) return NormalTotal;
	return ImpureTotal;
}

int32 FSFPResourceNodeAvailability::OccupiedForPurity(const FString& Purity) const
{
	if (Purity.Equals(TEXT("Rein"), ESearchCase::IgnoreCase)) return PureOccupied;
	if (Purity.Equals(TEXT("Normal"), ESearchCase::IgnoreCase)) return NormalOccupied;
	return ImpureOccupied;
}

int32 FSFPResourceNodeAvailability::FreeForPurity(const FString& Purity) const
{
	return FMath::Max(0, TotalForPurity(Purity) - OccupiedForPurity(Purity));
}

bool SFPResourceNodeInventory::Scan(
	UWorld* World,
	TMap<FString, FSFPResourceNodeAvailability>& OutAvailability,
	FString& OutError)
{
	OutAvailability.Reset();
	OutError.Reset();
	if (!IsValid(World))
	{
		OutError = TEXT("Keine geladene Spielwelt für die Rohstoffinventur");
		return false;
	}

	TArray<AFGResourceNode*> Nodes;
	TMap<AFGResourceNode*, int32> PurityByNode;
	TMap<AFGResourceNode*, FString> ResourceByNode;
	for (TActorIterator<AFGResourceNode> It(World); It; ++It)
	{
		AFGResourceNode* Node = *It;
		UClass* ResourceClass = ReadResourceClass(Node);
		const int32 Purity = ReadPurityIndex(Node);
		if (!IsValid(Node) || !IsValid(ResourceClass) || Purity == INDEX_NONE) continue;
		const FString ResourcePath = ResourceClass->GetPathName();
		if (ResourcePath.IsEmpty()) continue;
		Nodes.Add(Node);
		PurityByNode.Add(Node, Purity);
		ResourceByNode.Add(Node, ResourcePath);
		IncrementTotal(OutAvailability.FindOrAdd(ResourcePath), Purity);
	}

	TSet<AFGResourceNode*> OccupiedNodes;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Extractor = *It;
		if (!IsExtractorCandidate(Extractor)) continue;

		AFGResourceNode* MatchedNode = ReadLinkedNode(Extractor);
		if (!IsValid(MatchedNode) || !PurityByNode.Contains(MatchedNode))
		{
			MatchedNode = nullptr;
			double BestDistanceSquared = FMath::Square(MaximumExtractorNodeDistanceCm);
			for (AFGResourceNode* Candidate : Nodes)
			{
				if (!IsValid(Candidate) || OccupiedNodes.Contains(Candidate)) continue;
				const double DistanceSquared = FVector::DistSquared(
					Extractor->GetActorLocation(), Candidate->GetActorLocation());
				if (DistanceSquared < BestDistanceSquared)
				{
					BestDistanceSquared = DistanceSquared;
					MatchedNode = Candidate;
				}
			}
		}
		if (!IsValid(MatchedNode) || OccupiedNodes.Contains(MatchedNode)) continue;
		OccupiedNodes.Add(MatchedNode);
		IncrementOccupied(
			OutAvailability.FindOrAdd(ResourceByNode.FindRef(MatchedNode)),
			PurityByNode.FindRef(MatchedNode));
	}

	if (OutAvailability.IsEmpty())
	{
		OutError = TEXT("Keine Standard-Rohstoffquellen in der geladenen Welt gefunden");
		return false;
	}
	return true;
}

FString SFPResourceNodeInventory::ToJson(
	const TMap<FString, FSFPResourceNodeAvailability>& Availability)
{
	TArray<FString> Keys;
	Availability.GenerateKeyArray(Keys);
	Keys.Sort();
	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FString& Key : Keys)
	{
		const FSFPResourceNodeAvailability* Value = Availability.Find(Key);
		if (Value == nullptr) continue;
		TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("resource"), Key);
		Entry->SetNumberField(TEXT("impureTotal"), Value->ImpureTotal);
		Entry->SetNumberField(TEXT("impureOccupied"), Value->ImpureOccupied);
		Entry->SetNumberField(TEXT("normalTotal"), Value->NormalTotal);
		Entry->SetNumberField(TEXT("normalOccupied"), Value->NormalOccupied);
		Entry->SetNumberField(TEXT("pureTotal"), Value->PureTotal);
		Entry->SetNumberField(TEXT("pureOccupied"), Value->PureOccupied);
		Entries.Add(MakeShared<FJsonValueObject>(Entry));
	}
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetArrayField(TEXT("entries"), Entries);
	FString Json;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);
	return Json;
}

bool SFPResourceNodeInventory::FromJson(
	const FString& Json,
	TMap<FString, FSFPResourceNodeAvailability>& OutAvailability,
	FString& OutError)
{
	OutAvailability.Reset();
	OutError.Reset();
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = TEXT("Rohstoffinventur konnte nicht gelesen werden");
		return false;
	}
	const TArray<TSharedPtr<FJsonValue>>* Entries = nullptr;
	if (!Root->TryGetArrayField(TEXT("entries"), Entries) || Entries == nullptr)
	{
		OutError = TEXT("Rohstoffinventur enthält keine Einträge");
		return false;
	}
	for (const TSharedPtr<FJsonValue>& JsonValue : *Entries)
	{
		const TSharedPtr<FJsonObject> Entry = JsonValue.IsValid() && JsonValue->Type == EJson::Object
			? JsonValue->AsObject() : nullptr;
		if (!Entry.IsValid()) continue;
		FString Resource;
		if (!Entry->TryGetStringField(TEXT("resource"), Resource) || Resource.IsEmpty()) continue;
		FSFPResourceNodeAvailability Value;
		auto ReadCount = [&Entry](const TCHAR* Field)
		{
			double Number = 0.0;
			return Entry->TryGetNumberField(Field, Number)
				? FMath::Clamp(FMath::RoundToInt(Number), 0, 1000000) : 0;
		};
		Value.ImpureTotal = ReadCount(TEXT("impureTotal"));
		Value.ImpureOccupied = FMath::Min(Value.ImpureTotal, ReadCount(TEXT("impureOccupied")));
		Value.NormalTotal = ReadCount(TEXT("normalTotal"));
		Value.NormalOccupied = FMath::Min(Value.NormalTotal, ReadCount(TEXT("normalOccupied")));
		Value.PureTotal = ReadCount(TEXT("pureTotal"));
		Value.PureOccupied = FMath::Min(Value.PureTotal, ReadCount(TEXT("pureOccupied")));
		OutAvailability.Add(Resource, Value);
	}
	return true;
}
