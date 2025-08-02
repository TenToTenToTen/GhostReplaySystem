// Fill out your copyright notice in the Description page of Project Settings.


#include "BloodStainManager.h"

#include "BloodStainSubsystem.h"
#include "BloodStainSystem.h"
#include "GhostData.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

// Sets default values
ABloodStainManager::ABloodStainManager()
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
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;
}

ABloodStainActor* ABloodStainManager::PerformSpawnBloodStain(const FString& FileName, const FString& LevelName)
{
	if (IsNetMode(NM_Standalone))
	{
		SpawnBloodStain_Implementation(FileName, LevelName);
	}
	else
	{
		SpawnBloodStain(FileName, LevelName);
	}
	return CachedBloodStainActor;
}

void ABloodStainManager::SpawnBloodStain_Implementation(const FString& FileName, const FString& LevelName)
{
	FRecordHeaderData RecordHeaderData;

	UWorld* World = GetWorld();
	if (World)
	{
		if (const UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UBloodStainSubsystem* BloodStainSubsystem = GameInstance->GetSubsystem<UBloodStainSubsystem>())
			{
				if (!BloodStainSubsystem->FindOrLoadRecordHeader(FileName, LevelName, RecordHeaderData))
				{
					UE_LOG(LogBloodStain, Warning, TEXT("Failed to SpawnBloodStain. cannot Load Header Filename:[%s]"), *FileName);
					return;
				}
			}
		}
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

		CachedBloodStainActor = World->SpawnActor<ABloodStainActor>(BloodStainActorClass, Location, Rotation, Params);

		if (!CachedBloodStainActor)
		{
			UE_LOG(LogBloodStain, Error, TEXT("[BloodStain] Failed to spawn BloodStainActor at %s"), *Location.ToString());
			return;
		}

		CachedBloodStainActor->Initialize(FileName, LevelName);

		return;
	}

	UE_LOG(LogBloodStain, Warning, TEXT("Failed to LineTrace to Floor."))
}

