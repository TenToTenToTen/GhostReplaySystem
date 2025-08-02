// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GhostData.h"
#include "GameFramework/Actor.h"
#include "BloodStainManager.generated.h"

class ABloodStainActor;

UCLASS()
class BLOODSTAINSYSTEM_API ABloodStainManager : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	ABloodStainManager();

	ABloodStainActor* PerformSpawnBloodStain(const FString& FileName, const FString& LevelName);

private:
	UFUNCTION(Server, Reliable)
	void SpawnBloodStain(const FString& FileName, const FString& LevelName);

	UPROPERTY()
	TSubclassOf<ABloodStainActor> BloodStainActorClass;

	UPROPERTY()
	TObjectPtr<ABloodStainActor> CachedBloodStainActor;
};
