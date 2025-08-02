/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#include "BloodStainActor.h"
#include "GameFramework/Pawn.h"
#include "Engine/GameInstance.h" 
#include "BloodStainSubsystem.h"
#include "BloodStainSystem.h"
#include "Components/DecalComponent.h"
#include "Components/SphereComponent.h"
#include "Blueprint/UserWidget.h"
#include "Net/UnrealNetwork.h"

FName ABloodStainActor::SphereComponentName(TEXT("InteractionSphere"));

ABloodStainActor::ABloodStainActor()
{
	SphereComponent = CreateDefaultSubobject<USphereComponent>(ABloodStainActor::SphereComponentName);

	bReplicates = true;
	SphereComponent->SetupAttachment(GetDecal());
	
	SphereComponent->InitSphereRadius(50.f);
	SphereComponent->SetCanEverAffectNavigation(false);
	SphereComponent->SetGenerateOverlapEvents(false);
	
	SphereComponent->OnComponentBeginOverlap.AddDynamic(this, &ABloodStainActor::OnOverlapBegin);
	SphereComponent->OnComponentEndOverlap.AddDynamic(this, &ABloodStainActor::OnOverlapEnd);
	
	InteractionWidgetClass = nullptr;
	InteractionWidgetInstance = nullptr;
}

void ABloodStainActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(ABloodStainActor, ReplayFileName);
	DOREPLIFETIME(ABloodStainActor, LevelName);
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
	if (!HasAuthority())
	{
		return;
	}
	
	APawn* PlayerPawn = Cast<APawn>(OtherActor);
	if (PlayerPawn)
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(PlayerPawn->GetController()))
		{
			if (InteractingPlayerController == nullptr)
			{
				InteractingPlayerController = PlayerController;
				SetOwner(PlayerController);
				Client_ShowInteractionWidget();
			}
		}
	}
}

void ABloodStainActor::OnOverlapEnd_Implementation(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	if (!HasAuthority())
	{
		return;
	}
	
	APawn* PlayerPawn = Cast<APawn>(OtherActor);
	if (PlayerPawn)
	{
		if (APlayerController* PlayerController = Cast<APlayerController>(PlayerPawn->GetController()))
		{
			if (InteractingPlayerController == PlayerController)
			{
				Client_HideInteractionWidget();
				SetOwner(nullptr);
				InteractingPlayerController = nullptr;
			}
		}
	}
}

void ABloodStainActor::Interact()
{
	UE_LOG(LogBloodStain, Log, TEXT("LocalRole: %d, HasAuthority:%d, NetMode:%d"), static_cast<int32>(GetLocalRole()), HasAuthority(), GetNetMode());
	if (GetLocalRole() < ENetRole::ROLE_Authority)
	{
		Server_Interact();
	}
	else
	{
		StartReplay();
	}
}

void ABloodStainActor::StartReplay()
{
	if (const UWorld* World = GetWorld())
	{
		if (UBloodStainSubsystem* Subsystem = World->GetGameInstance()->GetSubsystem<UBloodStainSubsystem>())
		{
			if (bAllowMultiplePlayback || !Subsystem->IsPlaying(LastPlaybackKey))
			{
				Subsystem->StartReplayByBloodStain(this, LastPlaybackKey);
				if (GetOwner())
				{
					Client_HideInteractionWidget();
					// SetOwner(nullptr);
					// InteractingPlayerController = nullptr;
				}
			}
		}
	}
}

void ABloodStainActor::Server_Interact_Implementation()
{
	APlayerController* RequestingController = GetOwner<APlayerController>();

	if (RequestingController && RequestingController == InteractingPlayerController)
	{
		StartReplay();
	}
	else
	{
		UE_LOG(LogBloodStain, Warning, TEXT("Server_Interact called by %s, but not the interacting player controller."), *GetNameSafe(RequestingController));
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

void ABloodStainActor::Client_ShowInteractionWidget_Implementation()
{
	if (InteractionWidgetClass && !InteractionWidgetInstance)
	{
		APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
		if (PlayerController && PlayerController->IsLocalController())
		{
			InteractionWidgetInstance = CreateWidget<UUserWidget>(PlayerController, InteractionWidgetClass);
			if (InteractionWidgetInstance)
			{
				InteractionWidgetInstance->AddToViewport();
				UE_LOG(LogBloodStain, Log, TEXT("Interaction widget SHOWN for %s on client."), *GetName());
			}
		}
	}
}

void ABloodStainActor::Client_HideInteractionWidget_Implementation()
{
	if (InteractionWidgetInstance)
	{
		InteractionWidgetInstance->RemoveFromParent();
		InteractionWidgetInstance = nullptr;
		UE_LOG(LogBloodStain, Log, TEXT("Interaction widget HIDDEN for %s on client."), *GetName());
	}
}
