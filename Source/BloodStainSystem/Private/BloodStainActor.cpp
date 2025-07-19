/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#include "BloodStainActor.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h" 
#include "BloodStainSubsystem.h"
#include "Components/DecalComponent.h"
#include "Components/SphereComponent.h"
#include "Blueprint/UserWidget.h"

FName ABloodStainActor::SphereComponentName(TEXT("InteractionSphere"));

ABloodStainActor::ABloodStainActor()
{
	SphereComponent = CreateDefaultSubobject<USphereComponent>(ABloodStainActor::SphereComponentName);

	SphereComponent->SetupAttachment(GetDecal());
	
	SphereComponent->InitSphereRadius(50.f);
	SphereComponent->SetCanEverAffectNavigation(false);
	SphereComponent->SetGenerateOverlapEvents(false);
	
	SphereComponent->OnComponentBeginOverlap.AddDynamic(this, &ABloodStainActor::OnOverlapBegin);
	SphereComponent->OnComponentEndOverlap.AddDynamic(this, &ABloodStainActor::OnOverlapEnd);
	
	InteractionWidgetClass = nullptr;
	InteractionWidgetInstance = nullptr;
}

void ABloodStainActor::Initialize(const FString& InReplayFileName, const FString& InLevelName)
{
	ReplayFileName = InReplayFileName;
	LevelName = InLevelName;

	SphereComponent->SetCollisionProfileName(TEXT("OverlapAll"));
	SphereComponent->SetGenerateOverlapEvents(true);
	SphereComponent->UpdateOverlaps();
}

void ABloodStainActor::OnOverlapBegin_Implementation(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* PlayerPawn = Cast<APawn>(OtherActor);
	if (PlayerPawn && PlayerPawn->IsLocallyControlled())
	{
		if (InteractionWidgetClass && !InteractionWidgetInstance)
		{
			if (APlayerController* PlayerController = Cast<APlayerController>(PlayerPawn->GetController()))
			{
				InteractionWidgetInstance = CreateWidget<UUserWidget>(PlayerController, InteractionWidgetClass);
				if (InteractionWidgetInstance)
				{
					InteractionWidgetInstance->AddToViewport();
					UE_LOG(LogTemp, Log, TEXT("Interaction widget created and shown for %s"), *GetName());
				}
			}
		}
	}
}

void ABloodStainActor::OnOverlapEnd_Implementation(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	APawn* PlayerPawn = Cast<APawn>(OtherActor);
	if (PlayerPawn && PlayerPawn->IsLocallyControlled())
	{
		if (InteractionWidgetInstance)
		{
			InteractionWidgetInstance->RemoveFromParent();
			InteractionWidgetInstance = nullptr;
			UE_LOG(LogTemp, Log, TEXT("Interaction widget removed for %s"), *GetName());
		}
	}
}

void ABloodStainActor::Interact()
{
	if (const UWorld* World = GetWorld())
	{
		if (UBloodStainSubsystem* Sub = World->GetGameInstance()->GetSubsystem<UBloodStainSubsystem>())
		{
			if (bAllowMultiplePlayback || !Sub->IsPlaying(LastPlaybackKey))
			{
				Sub->StartReplayByBloodStain(this, LastPlaybackKey);
			}
		}
	}
}

bool ABloodStainActor::GetHeaderData(FRecordHeaderData& OutRecordHeaderData)
{
	if (const UWorld* World = GetWorld())
	{
		if (UBloodStainSubsystem* Sub = World->GetGameInstance()->GetSubsystem<UBloodStainSubsystem>())
		{
			Sub->FindOrLoadRecordHeader(ReplayFileName, LevelName, OutRecordHeaderData);
			return true;
		}
	}

	return false;
}
