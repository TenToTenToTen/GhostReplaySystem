// Fill out your copyright notice in the Description page of Project Settings.


#include "BloodStainSubsystem.h"

#include "BloodActor.h"
#include "BloodStainFileUtils.h"
#include "BloodStainSystem.h"
#include "PlayComponent.h"
#include "RecordComponent.h"
#include "ReplayActor.h"
#include "Kismet/KismetMathLibrary.h"

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
}

bool UBloodStainSubsystem::StartRecording(AActor* TargetActor, const FBloodStainRecordOptions& Options)
{
	// 1) 유효성 검사
	if (!TargetActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartRecording failed: TargetActor is null."));
		return false;
	}
	
	if (ActiveRecorders.Contains(TargetActor))
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
	Recorder->Initialize(Options);

	// 5) Subsystem 맵에 등록 및 델리게이트 호출
	ActiveRecorders.Add(TargetActor, Recorder);
	// OnRecordStarted.Broadcast(TargetActor, Recorder);

	return true;
}

void UBloodStainSubsystem::StopRecording(AActor* TargetActor)
{
	// 1) 유효성 검사
	if (!TargetActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording failed: TargetActor is null."));
		return;
	}

	// 2) ActiveRecorders에서 컴포넌트 찾기
	URecordComponent** RecorderPtr = ActiveRecorders.Find(TargetActor);
	if (!RecorderPtr || !*RecorderPtr)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopRecording failed: No active recorder for %s"),
			   *TargetActor->GetName());
		return;
	}

	URecordComponent* Recorder = *RecorderPtr;
	Recorder->SaveQueuedFrames(); // Move Data from FrameQueue to GhostSaveData

	// 1. 기본 식별자 (플레이어 이름, 맵 이름 등)
	const FString MapName = GetWorld()->GetMapName().Replace(TEXT("/Game/Maps/"), TEXT(""));
	const FString PlayerIdentifier = TargetActor->GetFName().ToString();

	// 2. 고유성 보장 (타임스탬프)
	const FString UniqueTimestamp = FDateTime::Now().ToString(TEXT("%Y%m%d-%H%M%S%s")); // %s는 밀리초까지 포함
	
	if (!FBloodStainFileUtils::SaveToFile(Recorder->GetGhostSaveData(), FString::Printf(TEXT("%s-%s-%s.sav"), *MapName, *PlayerIdentifier, *UniqueTimestamp), FileSaveOptions))
	{
		UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] SaveToFile failed"));
	}
	
	// 3) 언레지스터
	Recorder->UnregisterComponent();

	// 4) Actor의 InstanceComponents 목록에서 제거
	TargetActor->RemoveInstanceComponent(Recorder);

	// 5) 컴포넌트 파괴 (mark for kill)
	Recorder->DestroyComponent();

	// 6) 맵에서 제거
	ActiveRecorders.Remove(TargetActor);

	UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] Recording stopped for %s"), *TargetActor->GetName());
}

bool UBloodStainSubsystem::StartReplay(AActor* TargetActor, const FRecordSavedData& Data)
{
	if (!TargetActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StartReplay failed: Actor is null"));
		return false;
	}
	if (ActiveReplayers.Contains(TargetActor))
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] Already replaying %s"), *TargetActor->GetName());
		return false;
	}
	
	AActor* GhostActor = GetWorld()->SpawnActor(AReplayActor::StaticClass(), &Data.RecordedFrames[0].ComponentTransforms[Data.SpawnPointComponentName]);
	
	UPlayComponent* Replayer = NewObject<UPlayComponent>(
		GhostActor, UPlayComponent::StaticClass(), NAME_None, RF_Transient);
	if (!Replayer)
	{
		UE_LOG(LogTemp, Error, TEXT("[BloodStain] Cannot create ReplayComponent on %s"), *GhostActor->GetName());
		return false;
	}
	
	GhostActor->AddInstanceComponent(Replayer);
	Replayer->RegisterComponent();
	
	Replayer->Initialize(Data);
	
	ActiveReplayers.Add(GhostActor, Replayer);
	// OnReplayStarted.Broadcast(TargetActor, Replayer);
	return true;
}

void UBloodStainSubsystem::StopReplay(AActor* TargetActor)
{
	if (!TargetActor)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: TargetActor is null."));
		return;
	}

	UPlayComponent** ReplayPtr = ActiveReplayers.Find(TargetActor);
	if (!ReplayPtr || !*ReplayPtr)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] StopReplay failed: No active replayer for %s"),
			   *TargetActor->GetName());
		return;
	}

	UPlayComponent* ReplayComp = *ReplayPtr;
	ReplayComp->SetComponentTickEnabled(false);
	ReplayComp->UnregisterComponent();
	TargetActor->RemoveInstanceComponent(ReplayComp);
	ReplayComp->DestroyComponent();

	ActiveReplayers.Remove(TargetActor);

	TargetActor->Destroy();
	
	// OnReplayStopped.Broadcast(TargetActor, Replayer);
	UE_LOG(LogBloodStain, Log, TEXT("[BloodStain] StopReplay for %s"), *TargetActor->GetName());
}

bool UBloodStainSubsystem::LoadRecordingData(const FString& FileName, FRecordSavedData& OutData)
{
	// 1) 캐시 확인
	if (FRecordSavedData* Cached = CachedRecordings.Find(FileName))
	{
		OutData = *Cached;
		return true;
	}

	// 2) 캐시에 없으면 파일에서 로드
	FRecordSavedData Loaded;
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


bool UBloodStainSubsystem::StartReplayFromFile(AActor* TargetActor, const FString& FileName)
{
	FRecordSavedData Data;
	if (!LoadRecordingData(FileName, Data))
	{
		return false;
	}
	return StartReplay(TargetActor, Data);
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

void UBloodStainSubsystem::SpawnAllBloodStain()
{
	const int32 LoadedCount = FBloodStainFileUtils::LoadAllFiles(CachedRecordings);

	if (LoadedCount > 0)
	{
		UE_LOG(LogBloodStain, Log, TEXT("Subsystem successfully loaded %d recordings into cache."), LoadedCount);
		
		AvailableRecordings.Empty();
		CachedRecordings.GetKeys(AvailableRecordings);
		
		for (const TPair<FString, FRecordSavedData>& Pair : CachedRecordings)
		{
			const FString& FileName = Pair.Key;
			const FRecordSavedData& Data = Pair.Value;
			FVector StartLocation = Data.RecordedFrames[0].ComponentTransforms[Data.SpawnPointComponentName].GetLocation();
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

	// 현재 해당 액터를 기록 중인 RecordComponent를 찾습니다.
	if (URecordComponent* RecordComp = ActiveRecorders.FindRef(TargetActor))
	{
		// RecordComponent에 부착 사실을 알립니다.
		RecordComp->OnComponentAttached(NewComponent);
	}
}

void UBloodStainSubsystem::NotifyComponentDetached(AActor* TargetActor, UMeshComponent* DetachedComponent)
{
	if (!TargetActor || !DetachedComponent)
	{
		UE_LOG(LogBloodStain, Warning, TEXT("[BloodStain] NotifyComponentDetached failed: TargetActor or DetachedComponent is null."));
		return;
	}

	if (URecordComponent* Recorder = ActiveRecorders.FindRef(TargetActor))
	{
		// RecordComponent에 탈착 사실을 알립니다.
		Recorder->OnComponentDetached(DetachedComponent);
	}
}

void UBloodStainSubsystem::SetFileSaveOptions(const FBloodStainFileOptions& InOptions)
{
	FileSaveOptions = InOptions;
	// 필요 시 바로 Config에 저장하려면 SaveConfig(); 호출
}
