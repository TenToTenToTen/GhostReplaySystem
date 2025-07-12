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
#include "Kismet/GameplayStatics.h"

FName UBloodStainSubsystem::DefaultGroupName = TEXT("BloodStainReplay");

void UBloodStainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const FString PathToLoad = TEXT("/BloodStainSystem/BP_BloodActor.BP_BloodActor_C");
	BloodStainActorClass = StaticLoadClass(ABloodActor::StaticClass(), nullptr, *PathToLoad);

	if (!BloodStainActorClass)
	{
		// 로드에 실패하면 치명적인 오류를 로그에 남깁니다.
		// 이 서브시스템의 핵심 기능이 동작하지 않을 것이기 때문입니다.
		UE_LOG(LogBloodStain, Fatal, TEXT("Failed to load BloodActorClass at path: %s. Subsystem will not function."), *PathToLoad);
	}

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
	
	if (!FBloodStainFileUtils::SaveToFile(RecordSaveData, FileName, FileSaveOptions))
	{
		UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] SaveToFile failed"));
	}

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

bool UBloodStainSubsystem::StartReplay(ABloodActor* BloodStainActor, const FRecordSaveData& Data)
{
	if (!BloodStainActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartReplay failed: Actor is null"));
		return false;
	}

	FRecordSaveData RecordSaveData;
	if (!LoadRecordingData(BloodStainActor->ReplayFileName, RecordSaveData))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Cannot Load Recording Data %s"), *BloodStainActor->ReplayFileName);
		return false;
	}
		
	if (BloodStainPlaybackGroups.Contains(RecordSaveData.Header.GroupName))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Already replaying %s"), *RecordSaveData.Header.GroupName.ToString());
		return false;
	}
	
	FBloodStainPlaybackGroup BloodStainPlaybackGroup;

	for (const FRecordActorSaveData& RecordActorData : RecordSaveData.RecordActorDataArray)
	{
		// TODO SpawnPoint 각 Actor별로 분리
		FTransform StartTransform = Data.Header.SpawnPointTransform;
		AActor* GhostActor = GetWorld()->SpawnActor(AReplayActor::StaticClass(), &StartTransform);
	
		UPlayComponent* Replayer = NewObject<UPlayComponent>(
			GhostActor, UPlayComponent::StaticClass(), NAME_None, RF_Transient);
		if (!Replayer)
		{
			UE_LOG(LogTemp, Error, TEXT("[BloodStain] Cannot create ReplayComponent on %s"), *GhostActor->GetName());
			return false;
		}
	
		GhostActor->AddInstanceComponent(Replayer);
		Replayer->RegisterComponent();
	
		Replayer->Initialize(RecordSaveData.Header.GroupName, RecordActorData, RecordSaveData.Header.RecordOptions);

		BloodStainPlaybackGroup.ActiveReplayers.Add(GhostActor, Replayer);
	}
	
	BloodStainPlaybackGroups.Add(RecordSaveData.Header.GroupName, BloodStainPlaybackGroup);
	// OnReplayStarted.Broadcast(TargetActor, Replayer);
	return true;
}

void UBloodStainSubsystem::StopReplay(FName GroupName)
{
	if (!BloodStainPlaybackGroups.Contains(GroupName))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: Group [%s] is not exist"), *GroupName.ToString());
		return;
	}

	FBloodStainPlaybackGroup& BloodStainPlaybackGroup = BloodStainPlaybackGroups[GroupName];

	for (const auto& [GhostActor, PlayComponent] : BloodStainPlaybackGroup.ActiveReplayers)
	{
		// StopReplayPlayComponent 으로 하면 재귀 발생함.
		GhostActor->Destroy();
	}
	
	BloodStainPlaybackGroup.ActiveReplayers.Empty();

	BloodStainPlaybackGroups.Remove(GroupName);	
}

void UBloodStainSubsystem::StopReplayPlayComponent(AActor* GhostActor)
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

	const FName& GroupName = PlayComponent->GetGroupName();
	if (!BloodStainPlaybackGroups.Contains(GroupName))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: Group [%s] is not exist"), *GroupName.ToString());
		return;
	}

	FBloodStainPlaybackGroup& BloodStainPlaybackGroup = BloodStainPlaybackGroups[GroupName];
	if (!BloodStainPlaybackGroup.ActiveReplayers.Contains(GhostActor))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: Group [%s] is not contains Actor [%s]"), *GroupName.ToString(), *GhostActor->GetActorLabel());
		return;
	}
	
	
	UPlayComponent* ReplayComp = BloodStainPlaybackGroup.ActiveReplayers[GhostActor];
	ReplayComp->SetComponentTickEnabled(false);
	ReplayComp->UnregisterComponent();
	GhostActor->RemoveInstanceComponent(ReplayComp);
	ReplayComp->DestroyComponent();
	
	BloodStainPlaybackGroup.ActiveReplayers.Remove(GhostActor);

	GhostActor->Destroy();
	
	// OnReplayStopped.Broadcast(GhostActor, Replayer);
	UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] StopReplay for %s"), *GhostActor->GetName());

	if (BloodStainPlaybackGroup.ActiveReplayers.Num() == 0)
	{
		StopReplay(GroupName);
	}
}

bool UBloodStainSubsystem::LoadRecordingData(const FString& FileName, FRecordSaveData& OutData)
{
	// 1) 캐시 확인
	if (FRecordSaveData* Cached = CachedRecordings.Find(FileName))
	{
		OutData = *Cached;
		return true;
	}

	// 2) 캐시에 없으면 파일에서 로드
	FRecordSaveData Loaded;
	if (!FBloodStainFileUtils::LoadFromFile(Loaded, FileName))
	{
		UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to load file %s"), *FileName);
		return false;
	}

	// 3) 캐시에 저장 & 반환
	CachedRecordings.Add(FileName, Loaded);
	OutData = MoveTemp(Loaded);
	return true;
}


bool UBloodStainSubsystem::StartReplayFromFile(ABloodActor* BloodStainActor, const FString& FileName)
{
	FRecordSaveData Data;
	if (!LoadRecordingData(FileName, Data))
	{
		return false;
	}
	return StartReplay(BloodStainActor, Data);
}

ABloodActor* UBloodStainSubsystem::SpawnBloodStain(const FVector& Location, const FRotator& Rotation, const FString& FileName)
{
	// 파일 유효성 검사
	if (!AvailableRecordings.Contains(FileName))
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
	BloodStain->Initialize(FileName);
	ActiveBloodStains.Add(BloodStain);

	return BloodStain;
}

void UBloodStainSubsystem::RemoveBloodStain(ABloodActor* StainActor)
{
	ActiveBloodStains.Remove(TWeakObjectPtr<ABloodActor>(StainActor));
	StainActor->Destroy();
}

void UBloodStainSubsystem::SpawnAllBloodStainInLevel()
{
	const int32 LoadedCount = FBloodStainFileUtils::LoadAllFiles(CachedRecordings, UGameplayStatics::GetCurrentLevelName(GetWorld()));

	// TODO - Spawn BloodStain
	
	if (LoadedCount > 0)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Subsystem successfully loaded %d recordings into cache."), LoadedCount);
		
		AvailableRecordings.Empty();
		CachedRecordings.GetKeys(AvailableRecordings);
		
		for (const TPair<FString, FRecordSaveData>& Pair : CachedRecordings)
		{
			const FString& FileName = Pair.Key;
			const FRecordSaveData& Data = Pair.Value;
			FVector StartLocation = Data.Header.SpawnPointTransform.GetLocation();
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
				FRotator Rotation;
				Rotation = UKismetMathLibrary::MakeRotFromZ(HitResult.Normal);
	
				SpawnBloodStain(Location, Rotation, FileName);
			}
			else
			{
				UE_LOG(LogBloodStain, Warning, TEXT("Failed to LineTrace to Floor."));
			}
		}
	}
	else
	{
		UE_LOG(LogBloodStain, Log, TEXT("No recordings were found or loaded."));
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

FRecordSaveData UBloodStainSubsystem::ConvertToSaveData(TArray<FRecordActorSaveData>& RecordActorDataArray, const FName& GroupName)
{
	FRecordSaveData RecordSaveData;
	RecordSaveData.Header.RecordOptions = BloodStainRecordGroups[GroupName].RecordOptions;
	RecordSaveData.Header.SpawnPointTransform = BloodStainRecordGroups[GroupName].SpawnPointTransform;
	RecordSaveData.Header.GroupName = GroupName;
	RecordSaveData.RecordActorDataArray = MoveTemp(RecordActorDataArray);
	return RecordSaveData;
}

void UBloodStainSubsystem::SetFileSaveOptions(const FBloodStainFileOptions& InOptions)
{
	FileSaveOptions = InOptions;
	// 필요 시 바로 Config에 저장하려면 SaveConfig(); 호출
}
