// Fill out your copyright notice in the Description page of Project Settings.


#include "BloodStainSubsystem.h"

#include "BloodActor.h"
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

FName UBloodStainSubsystem::DefaultGroupName = TEXT("BloodStainReplay");

UBloodStainSubsystem::UBloodStainSubsystem()
{
	static ConstructorHelpers::FClassFinder<ABloodActor> BloodActorClassFinder(TEXT("/BloodStainSystem/BP_BloodActor.BP_BloodActor_C"));

	if (BloodActorClassFinder.Succeeded())
	{
		BloodStainActorClass = BloodActorClassFinder.Class;
	}
	else
	{
		// 생성자에서는 Fatal 대신 Warning이나 Error를 사용하는 것이 더 안정적일 수 있습니다.
		UE_LOG(LogBloodStain, Fatal, TEXT("Failed to find BloodActorClass at path. Subsystem may not function."));
	}
}

void UBloodStainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReplayTerminatedActorManager = NewObject<UReplayTerminatedActorManager>(this, UReplayTerminatedActorManager::StaticClass(), "ReplayDeadActorManager");
}

bool UBloodStainSubsystem::StartRecording(AActor* TargetActor, const FBloodStainRecordOptions& Options, FName GroupName)
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

bool UBloodStainSubsystem::StartRecordingWithActors(TArray<AActor*> TargetActors, const FBloodStainRecordOptions& Options, FName GroupName)
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

void UBloodStainSubsystem::StopRecording(FName GroupName)
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

	if (BloodStainRecordGroup.ActiveRecorders.Num() == 0)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording failed: There is no active recorder. Record Group %s's Record is already Stopped"), GetData(GroupName.ToString()));
	}

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
		
		RecordComponent->SaveQueuedFrames(); // Move Data from FrameQueue to GhostSaveData

		FRecordActorSaveData RecordSaveData = RecordComponent->GetGhostSaveData();
		if (RecordSaveData.RecordedFrames.Num() == 0)
		{
			UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording Warning: Frame is 0: %s"), *Actor->GetName());
			continue;
		}
		RecordSaveDataArray.Add(RecordSaveData);
	}
	
	// 1. Destroy 시, 즉시 ActiveRecords에서 없애줄 필요가 있음
	// 1-2. Destroy 시, Destroyed에 즉시 추가해줘야됨.
	// 2. 풀링을 돌면서 (Option의 TimeBuffer에 의해 삭제 && ActiveRecord's num == 0) n초 이상 유지되면 StopRecording 하기 (삭제해주기)
	
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
	
	// Default Identifier
	const FString MapName = UGameplayStatics::GetCurrentLevelName(GetWorld());
	const FString GroupNameString = GroupName.ToString();

	// Provide Unique (TimeStamp)
	const FString UniqueTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S%s")); // %s는 밀리초까지 포함

	FTransform RootTransform = FTransform::Identity;
	for (const FRecordActorSaveData& SaveData : RecordSaveDataArray)
	{
		FTransform Transform = SaveData.RecordedFrames[0].ComponentTransforms[SaveData.ComponentName.ToString()];
		RootTransform += Transform;
	}

	RootTransform.SetLocation(RootTransform.GetLocation() / RecordSaveDataArray.Num());
	// Rotation는 정확할 필요는 없음.
	RootTransform.NormalizeRotation();
	RootTransform.SetScale3D(RootTransform.GetScale3D() / RecordSaveDataArray.Num());
	
	// 여러 Actor 사이의 중간 위치
	BloodStainRecordGroup.SpawnPointTransform = RootTransform;
	
	FRecordSaveData RecordSaveData = ConvertToSaveData(RecordSaveDataArray, GroupName);
	
	const FString FileName = FString::Printf(TEXT("/%s/%s-%s.sav"), *MapName, *GroupNameString, *UniqueTimestamp);
	
	// if (!FBloodStainFileUtils::SaveToFile(RecordSaveData, FileName, FileSaveOptions))
	// {
	// 	UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] SaveToFile failed"));
	// }

	 (new FAutoDeleteAsyncTask<FSaveRecordingTask>(
		 MoveTemp(RecordSaveData), FileName,FileSaveOptions
	 ))->StartBackgroundTask();	

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

void UBloodStainSubsystem::StopRecordComponent(URecordComponent* RecordComponent)
{
	if (!RecordComponent)
	{
		// TODO UE_Log
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
	
	if (!BloodStainRecordGroups[GroupName].ActiveRecorders.Contains(RecordComponent->GetOwner()))
	{
		UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] StopRecording stopped. In Group [%s], no Record Actor [%s]"), GetData(GroupName.ToString()), GetData(RecordComponent->GetOwner()->GetName()));	
		return;
	}
	
	ReplayTerminatedActorManager->AddToRecordGroup(GroupName, RecordComponent);
	
	BloodStainRecordGroups[GroupName].ActiveRecorders.Remove(RecordComponent->GetOwner());	

	RecordComponent->UnregisterComponent();
	RecordComponent->GetOwner()->RemoveInstanceComponent(RecordComponent);
	RecordComponent->DestroyComponent();
}

bool UBloodStainSubsystem::StartReplayByBloodStain(ABloodActor* BloodStainActor, FGuid& OutGuid)
{
	if (!BloodStainActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartReplay failed: Actor is null"));
		return false;
	}
	
	return StartReplayFromFile(BloodStainActor->ReplayFileName, BloodStainActor->LevelName, OutGuid);
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

bool UBloodStainSubsystem::IsFileBodyLoaded(const FString& FileName)
{
	return CachedRecordings.Contains(FileName);
}

bool UBloodStainSubsystem::FindOrLoadRecordBodyData(const FString& FileName, const FString& LevelName, FRecordSaveData& OutData)
{
	// 1) 캐시 확인
	if (FRecordSaveData* Cached = CachedRecordings.Find(FileName))
	{
		OutData = *Cached;
		return true;
	}

	// 2) 캐시에 없으면 파일에서 로드
	FRecordSaveData Loaded;
	if (!FBloodStainFileUtils::LoadFromFile(FileName, LevelName, Loaded))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to load file %s"), *FileName);
		return false;
	}

	// 3) 캐시에 저장 & 반환
	CachedRecordings.Add(FileName, Loaded);
	OutData = MoveTemp(Loaded);
	return true;
}

bool UBloodStainSubsystem::IsFileHeaderLoaded(const FString& FileName)
{
	return CachedHeaders.Contains(FileName);		
}

bool UBloodStainSubsystem::FindOrLoadRecordHeader(const FString& FileName, const FString& LevelName, FRecordHeaderData& OutRecordHeaderData)
{
	if (FRecordHeaderData* Cached = CachedHeaders.Find(FileName))
	{
		OutRecordHeaderData = *Cached;
		return true;
	}

	// 2) 캐시에 없으면 파일에서 로드
	FRecordHeaderData Loaded;
	if (!FBloodStainFileUtils::LoadHeaderFromFile(FileName, LevelName, Loaded))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to load file's Header %s"), *FileName);
		return false;
	}

	// 3) 캐시에 저장 & 반환
	CachedHeaders.Add(FileName, Loaded);
	OutRecordHeaderData = MoveTemp(Loaded);
	return true;
}

bool UBloodStainSubsystem::StartReplayFromFile(const FString& FileName, const FString& LevelName, FGuid& OutGuid)
{
	FRecordSaveData Data;
	if (!FindOrLoadRecordBodyData(FileName, LevelName, Data))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] File: Cannot Load File [%s]"), *FileName);
		return false;
	}
	return StartReplay_Internal(Data, OutGuid);
}

ABloodActor* UBloodStainSubsystem::SpawnBloodStain(const FString& FileName, const FString& LevelName)
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

void UBloodStainSubsystem::RemoveBloodStain(ABloodActor* StainActor)
{
	ActiveBloodStains.Remove(TWeakObjectPtr<ABloodActor>(StainActor));
	StainActor->Destroy();
}

void UBloodStainSubsystem::SpawnAllBloodStainInLevel()
{
	FString LevelName = UGameplayStatics::GetCurrentLevelName(GetWorld());
	// TODO - 고민, Cached를 완전히 지워버리고 있음.
	const int32 LoadedCount = FBloodStainFileUtils::LoadHeadersForAllFiles(CachedHeaders, LevelName);
	
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
	RecordSaveData.Header.RecordOptions = BloodStainRecordGroups[GroupName].RecordOptions;
	RecordSaveData.RecordActorDataArray = MoveTemp(RecordActorDataArray);
	return RecordSaveData;
}

ABloodActor* UBloodStainSubsystem::SpawnBloodStain_Internal(const FVector& Location, const FRotator& Rotation, const FString& FileName, const FString& LevelName)
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

	ABloodActor* BloodStain = World->SpawnActor<ABloodActor>(BloodStainActorClass, Location, Rotation, Params);

	if (!BloodStain)
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to spawn BloodStainActor at %s"), *Location.ToString());
		return nullptr;
	}

	// FileName 세팅 및 서브시스템 등록
	BloodStain->Initialize(FileName, LevelName);
	ActiveBloodStains.Add(BloodStain);

	return BloodStain;
}

bool UBloodStainSubsystem::StartReplay_Internal(const FRecordSaveData& RecordSaveData, FGuid& OutGuid)
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
			UE_LOG(LogTemp, Error, TEXT("[BloodStain] Cannot create ReplayComponent on %s"), *GhostActor->GetName());
			continue;
		}

		GhostActor->AddInstanceComponent(Replayer);
		Replayer->RegisterComponent();
	
		Replayer->Initialize(UniqueID, RecordActorData, Header.RecordOptions);

		BloodStainPlaybackGroup.ActiveReplayers.Add(GhostActor);
	}

	if (BloodStainPlaybackGroup.ActiveReplayers.Num() == 0)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Cannot Start Replay, Active Replay is zero"));
		return false;
	}
	OutGuid = UniqueID;
	BloodStainPlaybackGroups.Add(UniqueID, BloodStainPlaybackGroup);
	// OnReplayStarted.Broadcast(TargetActor, Replayer);
	return true;
}

void UBloodStainSubsystem::SetFileSaveOptions(const FBloodStainFileOptions& InOptions)
{
	FileSaveOptions = InOptions;
	// 필요 시 바로 Config에 저장하려면 SaveConfig(); 호출
}
