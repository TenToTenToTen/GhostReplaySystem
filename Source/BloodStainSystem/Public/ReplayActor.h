/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/


#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ReplayActor.generated.h"

UCLASS()
class BLOODSTAINSYSTEM_API AReplayActor : public AActor
{
	GENERATED_BODY()
	
public:	
	AReplayActor();

	UPROPERTY()
	TObjectPtr<USceneComponent> SceneRoot;
};
