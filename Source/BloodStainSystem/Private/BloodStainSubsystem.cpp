/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#include "BloodStainSubsystem.h"

#include "BloodStainActor.h"
#include "BloodStainFileUtils.h"
#include "BloodStainSystem.h"
#include "PlayComponent.h"
#include "RecordComponent.h"
#include "ReplayActor.h"
#include "ReplayTerminatedActorManager.h"
#include "SaveRecordingTask.h"
#include "GameplayTagContainer.h"
#include "GhostPlayerController.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Algo/BinarySearch.h"
#include "Kismet/KismetMathLibrary.h"

class FSaveRecordingTask;

float UBloodStainSubsystem::LineTraceLength = 500.f;

UBloodStainSubsystem::UBloodStainSubsystem()
{
	static ConstructorHelpers::FClassFinder<ABloodStainActor> BloodStainActorClassFinder(TEXT("/BloodStainSystem/BP_BloodStainActor.BP_BloodStainActor_C"));

	if (BloodStainActorClassFinder.Succeeded())
	{
		BloodStainActorClass = BloodStainActorClassFinder.Class;
	}
	else
	{
		UE_LOG(LogBloodStain, Fatal, TEXT("Failed to find BloodStainActorClass at path. Subsystem may not function."));
	}
}

void UBloodStainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReplayTerminatedActorManager = NewObject<UReplayTerminatedActorManager>(this, UReplayTerminatedActorManager::StaticClass(), "ReplayDeadActorManager");
	ReplayTerminatedActorManager->OnRecordGroupRemoveByCollecting.BindUObject(this, &UBloodStainSubsystem::CleanupInvalidRecordGroups);
	OnBloodStainReady.AddDynamic(this, &UBloodStainSubsystem::HandleBloodStainReady);
}

bool UBloodStainSubsystem::StartRecording(AActor* TargetActor, FBloodStainRecordOptions RecordOptions)
{
	if (!TargetActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartRecording failed: TargetActor is null."));
		return false;
	}

	if (!BloodStainRecordGroups.Contains(RecordOptions.RecordingGroupName))
	{
		FBloodStainRecordGroup RecordGroup;
		RecordGroup.RecordOptions = RecordOptions;
		if (const UWorld* World = GetWorld())
		{
			RecordGroup.WorldBaseGroupStartTime = World->GetTimeSeconds();
		}
		BloodStainRecordGroups.Add(RecordOptions.RecordingGroupName, RecordGroup);
	}
	
	FBloodStainRecordGroup& RecordGroup = BloodStainRecordGroups[RecordOptions.RecordingGroupName];
	
	if (RecordGroup.ActiveRecorders.Contains(TargetActor))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Already recording actor %s"), *TargetActor->GetName());
		return false;
	}

	URecordComponent* Recorder = NewObject<URecordComponent>(
		TargetActor,URecordComponent::StaticClass(), NAME_None,RF_Transient);
	
	if (!Recorder)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to create RecordComponent for %s"), *TargetActor->GetName());
		return false;
	}
	
	TargetActor->AddInstanceComponent(Recorder);
	Recorder->RegisterComponent();
	Recorder->Initialize(RecordGroup.RecordOptions, RecordGroup.WorldBaseGroupStartTime);

	RecordGroup.ActiveRecorders.Add(TargetActor, Recorder);
	
	return true;
}

bool UBloodStainSubsystem::StartRecordingWithActors(TArray<AActor*> TargetActors, FBloodStainRecordOptions RecordOptions)
{
	if (TargetActors.Num() == 0)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartRecording failed: TargetActor is null."));
		return false;
	}
	
	bool bRecordSucceed = false;
	
	for (AActor* TargetActor : TargetActors)
	{
		if (StartRecording(TargetActor, RecordOptions))
		{
			bRecordSucceed = true;
		}
	}
	
	return bRecordSucceed;
}

void UBloodStainSubsystem::StopRecording(FName GroupName, bool bSaveRecordingData)
{	
	if (!BloodStainRecordGroups.Contains(GroupName))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording failed: Record Group %s is not recording"), GetData(GroupName.ToString()));
		return;
	}

	FBloodStainRecordGroup& BloodStainRecordGroup = BloodStainRecordGroups[GroupName];
	
	if (bSaveRecordingData)
	{
		BloodStainRecordGroup.WorldBaseGroupEndTime = GetWorld()->GetTimeSeconds(); 
		const float FrameBaseEndTime = BloodStainRecordGroup.WorldBaseGroupEndTime - BloodStainRecordGroup.WorldBaseGroupStartTime;
		const float EffectiveStartTime = FrameBaseEndTime - BloodStainRecordGroup.RecordOptions.MaxRecordTime;
		const float FrameBaseStartTime = EffectiveStartTime > 0 ? EffectiveStartTime : 0;

		TMap<FName, int32> ActorNameToRecordDataIndexMap;
		TArray<FRecordActorSaveData> RecordActorSaveDataArray;
		TArray<FInstancedStruct> ActorHeaderDataArray;
		
		TArray<FName> TerminateActorNameArray;
		TArray<FInstancedStruct> TerminateRecordActorUserDataArray;
		TArray<FRecordActorSaveData> TerminatedActorSaveDataArray = ReplayTerminatedActorManager->CookQueuedFrames(GroupName, FrameBaseStartTime, TerminateActorNameArray, TerminateRecordActorUserDataArray);		
		for (int32 Index = 0; Index < TerminatedActorSaveDataArray.Num(); Index++)
		{
			const FRecordActorSaveData& RecordActorSaveData = TerminatedActorSaveDataArray[Index];
			const FName& ActorName = TerminateActorNameArray[Index];
			const FInstancedStruct& RecordActorUserData = TerminateRecordActorUserDataArray[Index];

			if (!RecordActorSaveData.IsValid())
			{
				UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Warning: Frame num is 0"));
				continue;
			}

			ActorHeaderDataArray.Add(RecordActorUserData);
			
			RecordActorSaveDataArray.Add(RecordActorSaveData);
			int32 RecordDataIndex = RecordActorSaveDataArray.Num() - 1;
			ActorNameToRecordDataIndexMap.Add(ActorName, RecordDataIndex);
		}
		for (const auto& [Actor, RecordComponent] : BloodStainRecordGroup.ActiveRecorders)
		{
			if (!Actor)
			{
				UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Warning: Actor is not Valid"));
				continue;
			}

			if (!RecordComponent)
			{
				UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Warning: RecordComponent is not Valid for Actor: %s"), *Actor->GetName());
				continue;
			}

			FRecordActorSaveData RecordSaveData = RecordComponent->CookQueuedFrames(FrameBaseStartTime);
			if (RecordSaveData.RecordedFrames.Num() == 0)
			{
				UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Warning: Frame is 0: %s"), *Actor->GetName());
				continue;
			}

			FInstancedStruct RecordActorUserData = RecordComponent->GetRecordActorUserData();
			ActorHeaderDataArray.Add(RecordActorUserData);
			
			RecordActorSaveDataArray.Add(RecordSaveData);
			int32 RecordDataIndex = RecordActorSaveDataArray.Num() - 1;
			ActorNameToRecordDataIndexMap.Add(Actor->GetFName(), RecordDataIndex);
		}

		
		if (RecordActorSaveDataArray.Num() == 0)
		{
			UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Failed: There is no Valid Recorder Group[%s]"), GetData(GroupName.ToString()));
			return;
		}
		
		const FString MapName = UGameplayStatics::GetCurrentLevelName(GetWorld());
		FString GroupNameString = GroupName.ToString();
		const FString UniqueTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S%s"));

		if (GroupName == NAME_None)
		{
			GroupNameString = DefaultGroupName.ToString();
		}

		if (BloodStainRecordGroup.RecordingMainActor.Get() != nullptr && ActorNameToRecordDataIndexMap.Contains(BloodStainRecordGroup.RecordingMainActor->GetFName()))
		{
			const int32 Index = ActorNameToRecordDataIndexMap[BloodStainRecordGroup.RecordingMainActor->GetFName()];
			const FRecordActorSaveData& SaveData = RecordActorSaveDataArray[Index];
			
			BloodStainRecordGroup.SpawnPointTransform = SaveData.RecordedFrames[0].ComponentTransforms[SaveData.PrimaryComponentName.ToString()];
		}
		else
		{
			const FRecordActorSaveData& SaveData = RecordActorSaveDataArray[0];
			BloodStainRecordGroup.SpawnPointTransform = SaveData.RecordedFrames[0].ComponentTransforms[SaveData.PrimaryComponentName.ToString()];
		}
	
		if (BloodStainRecordGroup.RecordOptions.FileName == NAME_None)
		{
			BloodStainRecordGroup.RecordOptions.FileName = FName(FString::Printf(TEXT("%s-%s"), *GroupNameString, *UniqueTimestamp));
		}
		
		FRecordSaveData RecordSaveData = ConvertToSaveData(FrameBaseEndTime, GroupName, BloodStainRecordGroup.RecordOptions.FileName, FName(MapName), RecordActorSaveDataArray);
		
		RecordSaveData.Header.RecordGroupUserData = GetReplayUserHeaderData(GroupName);
		RecordSaveData.Header.RecordActorUserData = ActorHeaderDataArray;
		

		const FString FinalFileName = FString::Printf(TEXT("BloodStainReplay-%s"), *UniqueTimestamp); 
		const FString FinalFilePath = BloodStainFileUtils::GetFullFilePath(FinalFileName, MapName);

		RecordSaveData.Header.FileName = FName(FinalFileName);
		RecordSaveData.Header.LevelName = FName(MapName);
		
		auto OnSaveCompleted = [this, FinalFilePath, Header = RecordSaveData.Header]()
		{
			if (GetWorld())
			{
				if (AGhostPlayerController* PC = Cast<AGhostPlayerController>(GetWorld()->GetFirstPlayerController()))
				{
					if (PC->IsLocalController())
					{
						UE_LOG(LogBloodStain, Log, TEXT("Async save completed. Starting upload for: %s"), *FinalFilePath);
						PC->StartFileUpload(FinalFilePath, Header);
					}
				}
			}
		};
		
		OnCompleteBuildRecordingHeader.Broadcast(GroupName);
		ClearReplayUserHeaderData(GroupName);
		
		(new FAutoDeleteAsyncTask<FSaveRecordingTask>(
			MoveTemp(RecordSaveData), MapName, BloodStainRecordGroup.RecordOptions.FileName.ToString(), FileSaveOptions
			, FSimpleDelegateGraphTask::FDelegate::CreateLambda(MoveTemp(OnSaveCompleted))
		))->StartBackgroundTask();
	}

	TArray<TObjectPtr<AActor>> Keys;
	BloodStainRecordGroup.ActiveRecorders.GetKeys(Keys);
	for (AActor* Actor : Keys)
	{
		if (URecordComponent* RecordComponent = BloodStainRecordGroup.ActiveRecorders.FindRef(Actor))
		{
			RecordComponent->UnregisterComponent();
			Actor->RemoveInstanceComponent(RecordComponent);
			RecordComponent->DestroyComponent();
		}
	}
	
	BloodStainRecordGroups.Remove(GroupName);
	ReplayTerminatedActorManager->ClearRecordGroup(GroupName);
	
	UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] Recording stopped for %s"), GetData(GroupName.ToString()));
}

void UBloodStainSubsystem::StopRecordComponent(URecordComponent* RecordComponent, bool bSaveRecordingData)
{
	if (!RecordComponent)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording failed: RecordComponent is null."));
		return;
	}
	const FName& GroupName = RecordComponent->GetRecordGroupName();

	if (!BloodStainRecordGroups.Contains(GroupName))
	{
		UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] StopRecording stopped. Group [%s] is not exist"), GetData(GroupName.ToString()));	
		return;
	}

	FBloodStainRecordGroup& BloodStainRecordGroup = BloodStainRecordGroups[GroupName];

	if (!BloodStainRecordGroup.ActiveRecorders.Contains(RecordComponent->GetOwner()))
	{
		UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] StopRecording stopped. In Group [%s], no Record Actor [%s]"), GetData(GroupName.ToString()), GetData(RecordComponent->GetOwner()->GetName()));	
		return;
	}
	
	if (bSaveRecordingData)
	{	
		ReplayTerminatedActorManager->AddToRecordGroup(GroupName, RecordComponent);	
	}
	
	BloodStainRecordGroup.ActiveRecorders.Remove(RecordComponent->GetOwner());	

	RecordComponent->UnregisterComponent();
	RecordComponent->GetOwner()->RemoveInstanceComponent(RecordComponent);
	RecordComponent->DestroyComponent();

	if (BloodStainRecordGroup.ActiveRecorders.IsEmpty())
	{
		if (BloodStainRecordGroup.RecordOptions.bSaveImmediatelyIfGroupEmpty)
		{
			StopRecording(GroupName, bSaveRecordingData);
		}
	}
}

bool UBloodStainSubsystem::StartReplayByBloodStain(APlayerController* RequestingController, ABloodStainActor* BloodStainActor, FGuid& OutGuid)
{
	if (!BloodStainActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartReplay failed: Actor is null"));
		return false;
	}
	
	return StartReplayFromFile(RequestingController, BloodStainActor->ReplayFileName, BloodStainActor->LevelName, OutGuid, BloodStainActor->PlaybackOptions);
}

bool UBloodStainSubsystem::StartReplayFromFile(APlayerController* RequestingController, const FString& FileName, const FString& LevelName, FGuid& OutGuid, FBloodStainPlaybackOptions PlaybackOptions)
{
	ENetMode NetMode;
	if (UWorld* World = GetWorld())
	{
		NetMode = World->GetNetMode();
	}

	if (NetMode == ENetMode::NM_Standalone)
	{
		FRecordSaveData Data;
		if (!FindOrLoadRecordBodyData(FileName, LevelName, Data))
		{
			UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] File: Cannot Load File [%s]"), *FileName);
			return false;
		}

		return StartReplay_Standalone(Data, PlaybackOptions, OutGuid);
	}
	else // NM_ListenServer or NM_DedicatedServer
	{
		FBloodStainFileHeader FileHeader;
		FRecordHeaderData RecordHeader;
		TArray<uint8> CompressedPayload;
	
		if (!BloodStainFileUtils::LoadRawPayloadFromFile(FileName, LevelName, FileHeader, RecordHeader, CompressedPayload))
		{
			UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] File: Cannot Load Raw Payload [%s] for Networked"), *FileName);
			return false;
		}
	
		return StartReplay_Networked(RequestingController, FileName, LevelName, FileHeader, RecordHeader, CompressedPayload, PlaybackOptions, OutGuid);
	}
	
}

void UBloodStainSubsystem::StopReplay(FGuid PlaybackKey)
{
	if (!BloodStainPlaybackGroups.Contains(PlaybackKey))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: Group [%s] is not exist"), *PlaybackKey.ToString());
		return;
	}

	FBloodStainPlaybackGroup& BloodStainPlaybackGroup = BloodStainPlaybackGroups[PlaybackKey];

	for (AReplayActor* GhostActor : BloodStainPlaybackGroup.ActiveReplayers)
	{
		GhostActor->Destroy();
	}
	
	BloodStainPlaybackGroup.ActiveReplayers.Empty();

	BloodStainPlaybackGroups.Remove(PlaybackKey);	
}

void UBloodStainSubsystem::StopReplayPlayComponent(AReplayActor* GhostActor)
{
	if (!GhostActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: TargetActor is null."));
		return;
	}

	UPlayComponent* PlayComponent = GhostActor->GetComponentByClass<UPlayComponent>();
	if (!PlayComponent)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: PlayComponent is null."));
		return;
	}

	const FGuid PlaybackKey = PlayComponent->GetPlaybackKey();
	if (!BloodStainPlaybackGroups.Contains(PlaybackKey))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: Key [%s] is not exist"), *PlaybackKey.ToString());
		return;
	}

	FBloodStainPlaybackGroup& BloodStainPlaybackGroup = BloodStainPlaybackGroups[PlaybackKey];
	if (!BloodStainPlaybackGroup.ActiveReplayers.Contains(GhostActor))
	{
#if WITH_EDITOR
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: Key [%s] is not contains Actor [%s]"), *PlaybackKey.ToString(), *GhostActor->GetActorLabel());
#endif
		return;
	}
	
	PlayComponent->SetComponentTickEnabled(false);
	PlayComponent->UnregisterComponent();
	GhostActor->RemoveInstanceComponent(PlayComponent);
	PlayComponent->DestroyComponent();
	
	BloodStainPlaybackGroup.ActiveReplayers.Remove(GhostActor);
	
	GhostActor->Destroy();
	
	UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] StopReplay for %s"), *GhostActor->GetName());

	if (BloodStainPlaybackGroup.ActiveReplayers.Num() == 0)
	{
		StopReplay(PlaybackKey);
	}
}

bool UBloodStainSubsystem::GetPlaybackGroup(const FGuid& InGuid, FBloodStainPlaybackGroup& OutBloodStainPlaybackGroup)
{
	if (BloodStainPlaybackGroups.Contains(InGuid))
	{
		OutBloodStainPlaybackGroup = BloodStainPlaybackGroups[InGuid];
		return true;
	}
	return false;
}

void UBloodStainSubsystem::NotifyComponentAttached(AActor* TargetActor, UMeshComponent* NewComponent)
{
	if (!TargetActor || !NewComponent)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] NotifyComponentAttached failed: TargetActor or NewComponent is null."));
		return;
	}

	if (URecordComponent* RecordComponent = TargetActor->GetComponentByClass<URecordComponent>())
	{
		RecordComponent->OnComponentAttached(NewComponent);
	}
}

void UBloodStainSubsystem::NotifyComponentDetached(AActor* TargetActor, UMeshComponent* DetachedComponent)
{
	if (!TargetActor || !DetachedComponent)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] NotifyComponentDetached failed: TargetActor or DetachedComponent is null."));
		return;
	}

	if (URecordComponent* RecordComponent = TargetActor->GetComponentByClass<URecordComponent>())
	{
		RecordComponent->OnComponentDetached(DetachedComponent);
	}
}

void UBloodStainSubsystem::SetRecordingGroupMainActor(AActor* TargetActor, FName GroupName)
{	
	if (BloodStainRecordGroups.Contains(GroupName))
	{
		FBloodStainRecordGroup& BloodStainRecordGroup = BloodStainRecordGroups[GroupName];
		BloodStainRecordGroup.RecordingMainActor = TargetActor;
	}
}

void UBloodStainSubsystem::HandleBloodStainReady(ABloodStainActor* ReadyActor)
{
	if (ReadyActor)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Subsystem received a ready actor on the client: %s"), *ReadyActor->GetName());
		BloodStainActors.Add(ReadyActor);
	}
}

bool UBloodStainSubsystem::IsFileHeaderLoaded(const FString& FileName, const FString& LevelName) const
{
	const FString RelativeFilePath = GetRelativeFilePath(FileName, LevelName);
	return CachedHeaders.Contains(RelativeFilePath);		
}

bool UBloodStainSubsystem::IsFileBodyLoaded(const FString& FileName, const FString& LevelName) const
{
	const FString RelativeFilePath = GetRelativeFilePath(FileName, LevelName);
	return CachedRecordings.Contains(RelativeFilePath);
}

bool UBloodStainSubsystem::FindOrLoadRecordHeader(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData)
{
	const FString RelativeFilePath = GetRelativeFilePath(FileName, LevelName);
	if (FRecordHeaderData* Cached = CachedHeaders.Find(RelativeFilePath))
	{
		OutRecordHeaderData = *Cached;
		return true;
	}

	FRecordHeaderData Loaded;
	if (!BloodStainFileUtils::LoadHeaderFromFile(FileName, LevelName, Loaded))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to load file's Header %s"), *FileName);
		return false;
	}

	CachedHeaders.Add(RelativeFilePath, Loaded);
	OutRecordHeaderData = MoveTemp(Loaded);
	return true;
}

bool UBloodStainSubsystem::FindOrLoadRecordBodyData(const FString& FileName, const FString& LevelName, FRecordSaveData& OutData)
{
	const FString RelativeFilePath = GetRelativeFilePath(FileName, LevelName);
	if (FRecordSaveData* Cached = CachedRecordings.Find(RelativeFilePath))
	{
		OutData = *Cached;
		return true;
	}

	FRecordSaveData Loaded;
	if (!BloodStainFileUtils::LoadFromFile(FileName, LevelName, Loaded))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to load file %s"), *FileName);
		return false;
	}

	CachedRecordings.Add(RelativeFilePath, Loaded);
	OutData = MoveTemp(Loaded);
	return true;
}

TArray<FRecordHeaderData> UBloodStainSubsystem::GetCachedHeaders() const
{
	TArray<FRecordHeaderData> Result;
	CachedHeaders.GenerateValueArray(Result);
	return Result;
}

TArray<FRecordHeaderData> UBloodStainSubsystem::GetCachedHeadersByTags(const FGameplayTagContainer& FilterTags) const
{
	TArray<FRecordHeaderData> Result;

	for (const auto& [RelativeFilePath, HeaderData] : CachedHeaders)
	{
		if (HeaderData.Tags.HasAll(FilterTags))
		{
			Result.Add(HeaderData);
		}
	}

	return Result;
}

int32 UBloodStainSubsystem::LoadAllHeadersInLevel(const FString& LevelName)
{
	FString LevelStr = LevelName;
	if (LevelStr.IsEmpty())
	{
		LevelStr = UGameplayStatics::GetCurrentLevelName(GetWorld());
	}
	return BloodStainFileUtils::LoadHeadersForAllFilesInLevel(CachedHeaders, LevelStr);
}

int32 UBloodStainSubsystem::LoadAllHeadersInLevels(const TArray<FString>& LevelNames)
{
	int32 HeaderCount = 0;
	for (const FString& LevelName : LevelNames)
	{
		HeaderCount += LoadAllHeadersInLevel(LevelName);
	}
	return HeaderCount;
}

void UBloodStainSubsystem::LoadAllHeaders()
{
	BloodStainFileUtils::LoadHeadersForAllFiles(CachedHeaders);
}

void UBloodStainSubsystem::ClearCachedBodyData(const FString& FileName, const FString& LevelName)
{
	const FString RelativeFilePath = GetRelativeFilePath(FileName, LevelName);
	if (CachedRecordings.Contains(RelativeFilePath))
	{
		CachedRecordings.Remove(RelativeFilePath);
	}
}

void UBloodStainSubsystem::ClearCachedData(const FString& FileName, const FString& LevelName)
{
	const FString RelativeFilePath = GetRelativeFilePath(FileName, LevelName);
	if (CachedHeaders.Contains(RelativeFilePath))
	{
		CachedHeaders.Remove(RelativeFilePath);
	}

	ClearCachedBodyData(FileName, LevelName);
}

void UBloodStainSubsystem::ClearAllCachedBodyData()
{
	CachedHeaders.Empty();
}

void UBloodStainSubsystem::ClearAllCachedData()
{
	CachedHeaders.Empty();
	CachedRecordings.Empty();
}

bool UBloodStainSubsystem::DeleteFile(const FString& FileName, const FString& LevelName)
{
	ClearCachedBodyData(FileName, LevelName);
	return BloodStainFileUtils::DeleteFile(FileName, LevelName);
}

FString UBloodStainSubsystem::GetFullFilePath(const FString& FileName, const FString& LevelName) const
{
	return BloodStainFileUtils::GetFullFilePath(FileName, LevelName);
}

FString UBloodStainSubsystem::GetRelativeFilePath(const FString& FileName, const FString& LevelName) const
{
	return BloodStainFileUtils::GetRelativeFilePath(FileName, LevelName);
}

TArray<FString> UBloodStainSubsystem::GetSavedLevelNames() const
{
	return BloodStainFileUtils::GetSavedLevelNames();
}

TArray<FString> UBloodStainSubsystem::GetSavedFileNames(const FString& LevelName) const
{
	return BloodStainFileUtils::GetSavedFileNames(LevelName);
}

void UBloodStainSubsystem::SpawnBloodStain(const FString& FileName, const FString& LevelName, const FBloodStainPlaybackOptions PlaybackOptions)
{
	if (UWorld* World = GetWorld())
	{
		ENetMode NetMode = World->GetNetMode();
		if (NetMode == ENetMode::NM_Standalone)
		{
			SpawnBloodStainStandalone_Internal(FileName, LevelName, PlaybackOptions);
			return;
		}
	}

	
	if (APlayerController* PC = GetWorld()->GetFirstPlayerController())
	{
		if (PC->IsLocalController())
		{
			if (AGhostPlayerController* GhostPC = Cast<AGhostPlayerController>(PC))
			{
				GhostPC->Server_SpawnBloodStain(FileName, LevelName, PlaybackOptions);
			}
		}
	}
	else
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Cannot find PlayerController"));
	}
}

TArray<ABloodStainActor*> UBloodStainSubsystem::SpawnAllBloodStainInLevel(const FBloodStainPlaybackOptions PlaybackOptions)
{
	const FString LevelName = UGameplayStatics::GetCurrentLevelName(GetWorld());

	const int32 LoadedCount = LoadAllHeadersInLevel(LevelName);

	TArray<ABloodStainActor*> SpawnedActors;
	if (LoadedCount > 0)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Subsystem successfully loaded %d recording Headers into cache."), LoadedCount);
		
		for (const auto& [RelativeFilePath, RecordHeaderData] : CachedHeaders)
		{
			const FString FileName = RecordHeaderData.FileName.ToString();
			SpawnBloodStain(FileName, LevelName, PlaybackOptions);
		}
	}
	else
	{
		UE_LOG(LogBloodStain, Log, TEXT("No recording Headers were found or loaded."));
	}
	return SpawnedActors;
}

bool UBloodStainSubsystem::IsPlaying(const FGuid& InPlaybackKey) const
{
	return BloodStainPlaybackGroups.Contains(InPlaybackKey);
}

FRecordSaveData UBloodStainSubsystem::ConvertToSaveData(float EndTime, const FName& GroupName, const FName& FileName, const FName& LevelName, TArray<FRecordActorSaveData>& RecordActorDataArray)
{
	FRecordSaveData RecordSaveData;

	RecordSaveData.Header.FileName = FileName;
	RecordSaveData.Header.LevelName = LevelName;
	RecordSaveData.Header.Tags = BloodStainRecordGroups[GroupName].RecordOptions.Tags;
	RecordSaveData.Header.SpawnPointTransform = BloodStainRecordGroups[GroupName].SpawnPointTransform;
	RecordSaveData.Header.MaxRecordTime = BloodStainRecordGroups[GroupName].RecordOptions.MaxRecordTime;
	RecordSaveData.Header.SamplingInterval = BloodStainRecordGroups[GroupName].RecordOptions.SamplingInterval;
	RecordSaveData.Header.TotalLength = FMath::Min(EndTime, BloodStainRecordGroups[GroupName].RecordOptions.MaxRecordTime);
	RecordSaveData.RecordActorDataArray = MoveTemp(RecordActorDataArray);
	
	return RecordSaveData;
}

void UBloodStainSubsystem::SetReplayUserGroupData(const FInstancedStruct& ReplayUserHeaderData, const FName GroupName)
{
	ReplayUserHeaderDataMap.Add(GroupName, ReplayUserHeaderData);
}

FInstancedStruct UBloodStainSubsystem::GetReplayUserHeaderData(const FName& GroupName)
{
	FInstancedStruct InstancedStruct;

	if (ReplayUserHeaderDataMap.Contains(GroupName))
	{
		InstancedStruct = ReplayUserHeaderDataMap[GroupName];
	}
	
	return InstancedStruct;
}

void UBloodStainSubsystem::ClearReplayUserHeaderData(const FName& GroupName)
{
	ReplayUserHeaderDataMap.Remove(GroupName);
}

bool UBloodStainSubsystem::StartReplay_Standalone(const FRecordSaveData& RecordSaveData, const FBloodStainPlaybackOptions& PlaybackOptions, FGuid& OutGuid)
{
	OutGuid = FGuid();

	if (!RecordSaveData.IsValid())
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartReplay failed: RecordActor is not valid"));
		return false;
	}

	const FRecordHeaderData& Header = RecordSaveData.Header;
	const TArray<FRecordActorSaveData>& ActorDataArray = RecordSaveData.RecordActorDataArray;

	const FGuid UniqueID = FGuid::NewGuid();
	
	FBloodStainPlaybackGroup BloodStainPlaybackGroup;

	for (const FRecordActorSaveData& ActorData : ActorDataArray)
	{
		// TODO : to separate all SpawnPoint data per Actors
		FTransform StartTransform = Header.SpawnPointTransform;
		AReplayActor* GhostActor = GetWorld()->SpawnActor<AReplayActor>(AReplayActor::StaticClass(), StartTransform);
		UPlayComponent* Replayer = GhostActor->GetPlayComponent();

		if (GhostActor)
		{
			GhostActor->SetActorHiddenInGame(true);
		}
		
		if (!Replayer)
		{
			UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Cannot create ReplayComponent on %s"), *GhostActor->GetName());
			continue;
		}		
		
		GhostActor->InitializeReplayLocal(UniqueID, Header, ActorData, PlaybackOptions);
		BloodStainPlaybackGroup.ActiveReplayers.Add(GhostActor);
	}

	if (BloodStainPlaybackGroup.ActiveReplayers.Num() == 0)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Cannot Start Replay, Active Replay is zero"));
		return false;
	}
	OutGuid = UniqueID;
	BloodStainPlaybackGroups.Add(UniqueID, BloodStainPlaybackGroup);
	return true;
}

bool UBloodStainSubsystem::StartReplay_Networked(APlayerController* RequestingController, const FString& FileName, const FString& LevelName,
	const FBloodStainFileHeader& FileHeader, const FRecordHeaderData& RecordHeader,
	const TArray<uint8>& CompressedPayload, const FBloodStainPlaybackOptions& PlaybackOptions, FGuid& OutGuid)
{
	OutGuid = FGuid::NewGuid();
	
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = RequestingController; 

	// Indicates AReplayActor's ownership to the controller
	AReplayActor* GhostActor = GetWorld()->SpawnActor<AReplayActor>(AReplayActor::StaticClass(), RecordHeader.SpawnPointTransform, SpawnParams);

	if (!GhostActor)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to spawn ReplayActor at %s"), *RecordHeader.SpawnPointTransform.GetLocation().ToString());
		return false;
	}

	GhostActor->Server_InitializeReplayWithPayload(RequestingController, OutGuid, FileHeader, RecordHeader, CompressedPayload, PlaybackOptions);

	FBloodStainPlaybackGroup PlaybackGroup;
	PlaybackGroup.ActiveReplayers.Add(GhostActor);
	BloodStainPlaybackGroups.Add(OutGuid, PlaybackGroup);

	return true;
}

bool UBloodStainSubsystem::IsValidReplayGroup(const FName& GroupName)
{
	if (!BloodStainRecordGroups.Contains(GroupName))
	{
		return false;
	}
	
	FBloodStainRecordGroup& BloodStainRecordGroup = BloodStainRecordGroups[GroupName];
	
	bool bActiveRecordEmpty = BloodStainRecordGroup.ActiveRecorders.IsEmpty();
	bool bRecordDataManaged = ReplayTerminatedActorManager->ContainsGroup(GroupName);

	if (bActiveRecordEmpty && !bRecordDataManaged)
	{
		return false;
	}

	return true;
}

void UBloodStainSubsystem::CleanupInvalidRecordGroups()
{
	TSet<FName> InvalidRecordGroups;
	for (const auto& [GroupName, BloodStainRecordGroup] : BloodStainRecordGroups)
	{
		if (!IsValidReplayGroup(GroupName))
		{
			InvalidRecordGroups.Add(GroupName);
		}
	}

	for (const FName& InvalidRecordGroupName : InvalidRecordGroups)
	{
		BloodStainRecordGroups.Remove(InvalidRecordGroupName);
		ReplayTerminatedActorManager->ClearRecordGroup(InvalidRecordGroupName);
	}
}

void UBloodStainSubsystem::SpawnBloodStainStandalone_Internal(const FString& FileName, const FString& LevelName,
	const FBloodStainPlaybackOptions& PlaybackOptions)
{
	UWorld* World = GetWorld();
	FRecordHeaderData RecordHeaderData;
	if (!FindOrLoadRecordHeader(FileName, LevelName, RecordHeaderData))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("Failed to SpawnBloodStain. cannot Load Header Filename:[%s]"), *FileName);
		return;
	}

	FVector StartLocation = RecordHeaderData.SpawnPointTransform.GetLocation();
	FVector EndLocation = StartLocation;
	EndLocation.Z -= UBloodStainSubsystem::LineTraceLength;
	FHitResult HitResult;
	FCollisionResponseParams ResponseParams;
	
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	if (World->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECC_WorldStatic, FCollisionQueryParams::DefaultQueryParam, ResponseParams))
	{
		FVector Location = HitResult.Location;
		FRotator Rotation = UKismetMathLibrary::MakeRotFromZ(HitResult.Normal);
		
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		ABloodStainActor* SpawnedActor  = World->SpawnActor<ABloodStainActor>(BloodStainActorClass, Location, Rotation, Params);
		
		if (!SpawnedActor)
		{
			UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to spawn BloodStainActor at %s"), *Location.ToString());
			return;
		}

		SpawnedActor->PlaybackOptions = PlaybackOptions;
		SpawnedActor->Initialize(FileName, LevelName);
		return;
	}
	UE_LOG(LogBloodStain, Warning, TEXT("Failed to LineTrace to Floor."))
}

void UBloodStainSubsystem::AddToPendingGroup(AActor* Actor, FName GroupName)
{
	if (!PendingGroups.Contains(GroupName))
	{
		PendingGroups.Add(GroupName, FPendingGroup());
	}

	FPendingActorData PendingActorData;
	PendingActorData.Actor = Actor;
	
	PendingGroups[GroupName].ActorData.Add(Actor->GetUniqueID(), PendingActorData);
}

void UBloodStainSubsystem::AddToPendingGroupWithActors(TArray<AActor*> Actors, FName GroupName)
{
	for (AActor* Actor : Actors)
	{
		AddToPendingGroup(Actor, GroupName);
	}
}

void UBloodStainSubsystem::RemoveFromPendingGroup(AActor* Actor, FName GroupName)
{
	if (PendingGroups.Contains(GroupName))
	{
		PendingGroups[GroupName].ActorData.Remove(Actor->GetUniqueID());
		if (PendingGroups[GroupName].ActorData.Num() == 0)
		{
			PendingGroups.Remove(GroupName);
		}
	}
}

void UBloodStainSubsystem::RemoveFromPendingGroupWithActors(TArray<AActor*> Actors, FName GroupName)
{
	for (AActor* Actor : Actors)
	{
		RemoveFromPendingGroup(Actor, GroupName);
	}
}

void UBloodStainSubsystem::StartRecordingWithPendingGroup(FBloodStainRecordOptions RecordOptions)
{
	if (PendingGroups.Contains(RecordOptions.RecordingGroupName))
	{
		FPendingGroup& PendingGroup = PendingGroups[RecordOptions.RecordingGroupName];
		for (auto& [Key, PendingActorData] : PendingGroup.ActorData)
		{
			if (PendingActorData.Actor.IsValid())
			{
				bool bSuccess = StartRecording(PendingActorData.Actor.Get(), RecordOptions);
				
				if (bSuccess && PendingActorData.InstancedStruct.IsValid())
				{
					BloodStainRecordGroups[RecordOptions.RecordingGroupName].ActiveRecorders[PendingActorData.Actor.Get()]->SetRecordActorUserData(PendingActorData.InstancedStruct);
				}
			}
		}

		if (PendingGroup.RecordingMainActor.Get() != nullptr)
		{
			SetRecordingGroupMainActor(PendingGroup.RecordingMainActor.Get(), RecordOptions.RecordingGroupName);
		}
		
		PendingGroups.Remove(RecordOptions.RecordingGroupName);
	}
}

void UBloodStainSubsystem::SetPendingGroupMainActor(AActor* TargetActor, FName GroupName)
{
	if (PendingGroups.Contains(GroupName))
	{
		FPendingGroup& PendingGroup = PendingGroups[GroupName];
		PendingGroup.RecordingMainActor = TargetActor;
	}
}

void UBloodStainSubsystem::SetPendingActorUserData(const FName GroupName, AActor* Actor, const FInstancedStruct& InInstancedStruct)
{
	if (PendingGroups.Contains(GroupName))
	{
		FPendingGroup& PendingGroup = PendingGroups[GroupName];

		if (PendingGroup.ActorData.Contains(Actor->GetUniqueID()))
		{
			FPendingActorData& PendingActorData = PendingGroup.ActorData[Actor->GetUniqueID()];
			PendingActorData.InstancedStruct = InInstancedStruct;
		}
	}
}

void UBloodStainSubsystem::HandleBeginFileUpload(AGhostPlayerController* Uploader, const FRecordHeaderData& Header,
	int64 FileSize)
{
	if (!Uploader)
	{
		return;
	}
    
	UE_LOG(LogBloodStain, Log, TEXT("Server: Begin receiving file '%s' from client %s. Size: %lld"), *Header.FileName.ToString(), *Uploader->GetName(), FileSize);
    
	FIncomingClientFile& TransferData = IncomingFileTransfers.FindOrAdd(Uploader);
	TransferData.Header = Header;
	TransferData.ExpectedSize = FileSize;
	TransferData.FileBuffer.Empty(FileSize);
}

void UBloodStainSubsystem::HandleReceiveFileChunk(AGhostPlayerController* Uploader, const TArray<uint8>& ChunkData)
{
	if (!Uploader)
	{
		return;
	}
    
	if (FIncomingClientFile* TransferData = IncomingFileTransfers.Find(Uploader))
	{
		TransferData->FileBuffer.Append(ChunkData);
	}
}

void UBloodStainSubsystem::HandleEndFileUpload(AGhostPlayerController* Uploader)
{
	if (!Uploader)
	{
		return;
	}

	if (FIncomingClientFile* TransferData = IncomingFileTransfers.Find(Uploader))
	{
		UE_LOG(LogBloodStain, Log, TEXT("Server: Finalized file transfer from client %s. Received %d bytes, Expected %lld bytes."), *Uploader->GetName(), TransferData->FileBuffer.Num(), TransferData->ExpectedSize);

		if (TransferData->FileBuffer.Num() == TransferData->ExpectedSize)
		{
			const FString FinalLevelName = TransferData->Header.LevelName.ToString();
			const FString FinalFileName = TransferData->Header.FileName.ToString();
			
			const FString FinalPath = BloodStainFileUtils::GetFullFilePath(FinalFileName, FinalLevelName);
			if (FFileHelper::SaveArrayToFile(TransferData->FileBuffer, *FinalPath))
			{
				UE_LOG(LogBloodStain, Log, TEXT("Server successfully saved client replay to: %s"), *FinalPath);
			}
			else
			{
				UE_LOG(LogBloodStain, Error, TEXT("Server failed to save client replay to: %s"), *FinalPath);
			}
		}
		else
		{
			UE_LOG(LogBloodStain, Warning, TEXT("File size mismatch for upload from %s. Upload failed."), *Uploader->GetName());
		}

		IncomingFileTransfers.Remove(Uploader);
	}
}
