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
#include "Kismet/KismetMathLibrary.h"
#include "SaveRecordingTask.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

class FSaveRecordingTask;

float UBloodStainSubsystem::LineTraceLength = 500.f;
FName UBloodStainSubsystem::DefaultGroupName = TEXT("BloodStainReplay");

UBloodStainSubsystem::UBloodStainSubsystem()
{
	static ConstructorHelpers::FClassFinder<ABloodStainActor> BloodStainActorClassFinder(TEXT("/BloodStainSystem/BP_BloodStainActor.BP_BloodStainActor_C"));

	if (BloodStainActorClassFinder.Succeeded())
	{
		BloodStainActorClass = BloodStainActorClassFinder.Class;
	}
	else
	{
		// 생성자에서는 Fatal 대신 Warning이나 Error를 사용하는 것이 더 안정적일 수 있습니다.
		UE_LOG(LogBloodStain, Fatal, TEXT("Failed to find BloodStainActorClass at path. Subsystem may not function."));
	}
}

void UBloodStainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReplayTerminatedActorManager = NewObject<UReplayTerminatedActorManager>(this, UReplayTerminatedActorManager::StaticClass(), "ReplayDeadActorManager");
	ReplayTerminatedActorManager->OnRecordGroupRemoveByCollecting.BindUObject(this, &UBloodStainSubsystem::CleanupInvalidRecordGroups);
}

bool UBloodStainSubsystem::StartRecording(AActor* TargetActor, FBloodStainRecordOptions Options, FName GroupName)
{
	if (!TargetActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartRecording failed: TargetActor is null."));
		return false;
	}

	if (GroupName == NAME_None)
	{
		GroupName = DefaultGroupName;
	}

	if (!BloodStainRecordGroups.Contains(GroupName))
	{
		FBloodStainRecordGroup RecordGroup;
		RecordGroup.RecordOptions = Options;
		BloodStainRecordGroups.Add(GroupName, RecordGroup);
	}
	
	FBloodStainRecordGroup& RecordGroup = BloodStainRecordGroups[GroupName];
	
	if (RecordGroup.ActiveRecorders.Contains(TargetActor))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Already recording actor %s"), *TargetActor->GetName());
		return false;
	}

	// 2) 컴포넌트 생성 & 등록
	URecordComponent* Recorder = NewObject<URecordComponent>(
		TargetActor,URecordComponent::StaticClass(), NAME_None,RF_Transient);
	
	if (!Recorder)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to create RecordComponent for %s"), *TargetActor->GetName());
		return false;
	}
	
	// Actor 생명주기에 포함
	TargetActor->AddInstanceComponent(Recorder);
	// Tick Activation
	Recorder->RegisterComponent();
	// Option Initialization
	Recorder->Initialize(GroupName, Options);

	RecordGroup.ActiveRecorders.Add(TargetActor, Recorder);
	
	return true;
}

bool UBloodStainSubsystem::StartRecordingWithActors(TArray<AActor*> TargetActors, FBloodStainRecordOptions Options, FName GroupName)
{
	if (TargetActors.Num() == 0)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartRecording failed: TargetActor is null."));
		return false;
	}
	
	bool bRecordSucceed = false;
	
	for (AActor* TargetActor : TargetActors)
	{
		if (StartRecording(TargetActor, Options, GroupName))
		{
			bRecordSucceed = true;
		}
	}
	
	return bRecordSucceed;
}

void UBloodStainSubsystem::StopRecording(FName GroupName, bool bSaveRecordingData)
{
	if (GroupName == NAME_None)
	{
		GroupName = DefaultGroupName;
	}
	
	if (!BloodStainRecordGroups.Contains(GroupName))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording failed: Record Group %s is not recording"), GetData(GroupName.ToString()));
		return;
	}

	FBloodStainRecordGroup& BloodStainRecordGroup = BloodStainRecordGroups[GroupName];
	
	if (bSaveRecordingData)
	{
		TArray<FRecordActorSaveData> RecordSaveDataArray;

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

			FRecordActorSaveData RecordSaveData = RecordComponent->CookQueuedFrames();
			if (RecordSaveData.RecordedFrames.Num() == 0)
			{
				UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Warning: Frame is 0: %s"), *Actor->GetName());
				continue;
			}
			RecordSaveDataArray.Add(RecordSaveData);
		}
		
		for (const FRecordActorSaveData& RecordActorSaveData : ReplayTerminatedActorManager->CookQueuedFrames(GroupName))
		{
			// Valid Data 검증 안해도 괜찮으려나
			if (RecordActorSaveData.RecordedFrames.Num() == 0)
			{
				UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Warning: Frame num is 0"));
				continue;
			}
			RecordSaveDataArray.Add(RecordActorSaveData);
		}
	
		if (RecordSaveDataArray.Num() == 0)
		{
			UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Failed: There is no Valid Recorder Group[%s]"), GetData(GroupName.ToString()));
			return;
		}
		
		const FString MapName = UGameplayStatics::GetCurrentLevelName(GetWorld());
		const FString GroupNameString = GroupName.ToString();
		const FString UniqueTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S%s")); // %s는 밀리초까지 포함

		FTransform RootTransform = FTransform::Identity;
		for (const FRecordActorSaveData& SaveData : RecordSaveDataArray)
		{
			FTransform Transform = SaveData.RecordedFrames[0].ComponentTransforms[SaveData.PrimaryComponentName.ToString()];
			RootTransform += Transform;
		}

		RootTransform.SetLocation(RootTransform.GetLocation() / RecordSaveDataArray.Num());
		RootTransform.NormalizeRotation();
		RootTransform.SetScale3D(RootTransform.GetScale3D() / RecordSaveDataArray.Num());
		BloodStainRecordGroup.SpawnPointTransform = RootTransform;
	
		FRecordSaveData RecordSaveData = ConvertToSaveData(RecordSaveDataArray, GroupName);
	
		const FString FileName = FString::Printf(TEXT("/%s-%s.sav"), *GroupNameString, *UniqueTimestamp);

		(new FAutoDeleteAsyncTask<FSaveRecordingTask>(
			MoveTemp(RecordSaveData), MapName, FileName,FileSaveOptions
		))->StartBackgroundTask();
	}
	
	BloodStainRecordGroups.Remove(GroupName);
	ReplayTerminatedActorManager->ClearRecordGroup(GroupName);
	
	for (const auto& [Actor, RecordComponent] : BloodStainRecordGroup.ActiveRecorders)
	{
		RecordComponent->UnregisterComponent();
		Actor->RemoveInstanceComponent(RecordComponent);
		RecordComponent->DestroyComponent();
	}
	
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
	// 주의사항
	// 즉시 ActiveRecords에서 없애줄 필요가 있음
	// Destroyed에 즉시 추가해줘야됨.
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

bool UBloodStainSubsystem::StartReplayByBloodStain(ABloodStainActor* BloodStainActor, FGuid& OutGuid)
{
	if (!BloodStainActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartReplay failed: Actor is null"));
		return false;
	}
	
	return StartReplayFromFile(BloodStainActor->ReplayFileName, BloodStainActor->LevelName, OutGuid, BloodStainActor->PlaybackOptions);
}

bool UBloodStainSubsystem::StartReplayFromFile(const FString& FileName, const FString& LevelName, FGuid& OutGuid, FBloodStainPlaybackOptions InPlaybackOptions)
{
	FRecordSaveData Data;
	if (!FindOrLoadRecordBodyData(FileName, LevelName, Data))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] File: Cannot Load File [%s]"), *FileName);
		return false;
	}
	return StartReplay_Internal(Data, InPlaybackOptions, OutGuid);
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
		// StopReplayPlayComponent 으로 하면 재귀 발생함.
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
	
	// OnReplayStopped.Broadcast(GhostActor, Replayer);
	UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] StopReplay for %s"), *GhostActor->GetName());

	if (BloodStainPlaybackGroup.ActiveReplayers.Num() == 0)
	{
		StopReplay(PlaybackKey);
	}
}

bool UBloodStainSubsystem::IsFileHeaderLoaded(const FString& FileName)
{
	return CachedHeaders.Contains(FileName);		
}

bool UBloodStainSubsystem::IsFileBodyLoaded(const FString& FileName)
{
	return CachedRecordings.Contains(FileName);
}

bool UBloodStainSubsystem::FindOrLoadRecordHeader(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData)
{
	if (FRecordHeaderData* Cached = CachedHeaders.Find(FileName))
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

	CachedHeaders.Add(FileName, Loaded);
	OutRecordHeaderData = MoveTemp(Loaded);
	return true;
}

bool UBloodStainSubsystem::FindOrLoadRecordBodyData(const FString& FileName, const FString& LevelName, FRecordSaveData& OutData)
{
	if (FRecordSaveData* Cached = CachedRecordings.Find(FileName))
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

	CachedRecordings.Add(FileName, Loaded);
	OutData = MoveTemp(Loaded);
	return true;
}

const TMap<FString, FRecordHeaderData>& UBloodStainSubsystem::GetCachedHeaders()
{
	return CachedHeaders;
}

void UBloodStainSubsystem::LoadAllHeadersInLevel(const FString& LevelName)
{
	FString LevelStr = LevelName;
	if (LevelStr.IsEmpty())
	{
		LevelStr = UGameplayStatics::GetCurrentLevelName(GetWorld());
	}
	BloodStainFileUtils::LoadHeadersForAllFiles(CachedHeaders, LevelStr);
}

FString UBloodStainSubsystem::GetFullFilePath(const FString& FileName, const FString& LevelName) const
{
	return BloodStainFileUtils::GetFullFilePath(FileName, LevelName);
}

ABloodStainActor* UBloodStainSubsystem::SpawnBloodStain(const FString& FileName, const FString& LevelName)
{
	FRecordHeaderData RecordHeaderData;

	if (!FindOrLoadRecordHeader(FileName, LevelName, RecordHeaderData))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("Failed to SpawnBloodStain. cannot Load Header Filename:[%s]"), *FileName);
		return nullptr;
	}

	FVector StartLocation = RecordHeaderData.SpawnPointTransform.GetLocation();
	FVector EndLocation = StartLocation;
	EndLocation.Z -= LineTraceLength;
	FHitResult HitResult;
	FCollisionResponseParams ResponseParams;
	//충돌 대상에서 Pawn 제외
	ResponseParams.CollisionResponse.SetResponse(ECC_Pawn, ECR_Ignore);
	bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, StartLocation, EndLocation, ECC_WorldStatic, FCollisionQueryParams::DefaultQueryParam, ResponseParams);
	if (bHit)
	{
		FVector Location = HitResult.Location;
		FRotator Rotation = UKismetMathLibrary::MakeRotFromZ(HitResult.Normal);
		return SpawnBloodStain_Internal(Location, Rotation, FileName, LevelName);
	}

	UE_LOG(LogBloodStain, Warning, TEXT("Failed to LineTrace to Floor."));
	return nullptr;
}

void UBloodStainSubsystem::SpawnAllBloodStainInLevel()
{
	FString LevelName = UGameplayStatics::GetCurrentLevelName(GetWorld());
	// TODO - 고민, Cached를 완전히 지워버리고 있음.
	const int32 LoadedCount = BloodStainFileUtils::LoadHeadersForAllFiles(CachedHeaders, LevelName);
	
	if (LoadedCount > 0)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Subsystem successfully loaded %d recording Headers into cache."), LoadedCount);
		
		for (const auto& [FileName, _] : CachedHeaders)
		{
			SpawnBloodStain(FileName, LevelName);
		}
	}
	else
	{
		UE_LOG(LogBloodStain, Log, TEXT("No recording Headers were found or loaded."));
	}
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

bool UBloodStainSubsystem::IsPlaying(const FGuid& InPlaybackKey) const
{
	return BloodStainPlaybackGroups.Contains(InPlaybackKey);
}

FRecordSaveData UBloodStainSubsystem::ConvertToSaveData(TArray<FRecordActorSaveData>& RecordActorDataArray, const FName& GroupName)
{
	FRecordSaveData RecordSaveData;
	RecordSaveData.Header.GroupName = GroupName;
	RecordSaveData.Header.SpawnPointTransform = BloodStainRecordGroups[GroupName].SpawnPointTransform;
	RecordSaveData.Header.MaxRecordTime = BloodStainRecordGroups[GroupName].RecordOptions.MaxRecordTime;
	RecordSaveData.Header.SamplingInterval = BloodStainRecordGroups[GroupName].RecordOptions.SamplingInterval;
	RecordSaveData.RecordActorDataArray = MoveTemp(RecordActorDataArray);
	return RecordSaveData;
}

ABloodStainActor* UBloodStainSubsystem::SpawnBloodStain_Internal(const FVector& Location, const FRotator& Rotation, const FString& FileName, const FString& LevelName)
{
	// 파일 유효성 검사
	if (!IsFileHeaderLoaded(FileName))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Invalid file '%s'"), *FileName);
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Cannot get world context")); 
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	ABloodStainActor* BloodStain = World->SpawnActor<ABloodStainActor>(BloodStainActorClass, Location, Rotation, Params);

	if (!BloodStain)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to spawn BloodStainActor at %s"), *Location.ToString());
		return nullptr;
	}

	// FileName 세팅 및 서브시스템 등록
	BloodStain->Initialize(FileName, LevelName);

	return BloodStain;
}

bool UBloodStainSubsystem::StartReplay_Internal(const FRecordSaveData& RecordSaveData, const FBloodStainPlaybackOptions& InPlaybackOptions, FGuid& OutGuid)
{
	OutGuid = FGuid();
	if (RecordSaveData.RecordActorDataArray.IsEmpty())
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartReplay failed: RecordActor is Empty"));
		return false;
	}

	// TODO - RecordSaveData가 Valid한지 체크

	const FRecordHeaderData& Header = RecordSaveData.Header;
	const TArray<FRecordActorSaveData>& RecordActorDataArray = RecordSaveData.RecordActorDataArray;

	const FGuid UniqueID = FGuid::NewGuid();
	
	FBloodStainPlaybackGroup BloodStainPlaybackGroup;

	for (const FRecordActorSaveData& RecordActorData : RecordActorDataArray)
	{
		// TODO SpawnPoint 각 Actor별로 분리
		FTransform StartTransform = Header.SpawnPointTransform;
		AReplayActor* GhostActor = GetWorld()->SpawnActor<AReplayActor>(AReplayActor::StaticClass(), StartTransform);
	
		UPlayComponent* Replayer = NewObject<UPlayComponent>(
			GhostActor, UPlayComponent::StaticClass(), NAME_None, RF_Transient);

		if (!Replayer)
		{
			UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Cannot create ReplayComponent on %s"), *GhostActor->GetName());
			continue;
		}

		GhostActor->AddInstanceComponent(Replayer);
		Replayer->RegisterComponent();
	
		Replayer->Initialize(UniqueID, Header, RecordActorData, InPlaybackOptions);

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

void UBloodStainSubsystem::SetFileSaveOptions(const FBloodStainFileOptions& InOptions)
{
	FileSaveOptions = InOptions;
}
