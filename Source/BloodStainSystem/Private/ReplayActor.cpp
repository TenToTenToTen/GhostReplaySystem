/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/



#include "ReplayActor.h"

// Sets default values
AReplayActor::AReplayActor()
{
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = Root;
	PrimaryActorTick.bCanEverTick = false;
}

// Called when the game starts or when spawned
void AReplayActor::BeginPlay()
{
	Super::BeginPlay();
	
}

// Called every frame
void AReplayActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

