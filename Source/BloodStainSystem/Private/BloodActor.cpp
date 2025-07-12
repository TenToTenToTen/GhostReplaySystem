// Fill out your copyright notice in the Description page of Project Settings.


#include "BloodActor.h"

#include "BloodStainSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Components/DecalComponent.h"
#include "Components/SphereComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"

FName ABloodActor::SphereComponentName(TEXT("InteractionSphere"));

ABloodActor::ABloodActor(const FObjectInitializer& ObjectInitializer)
{
	SphereComponent = CreateDefaultSubobject<USphereComponent>(ABloodActor::SphereComponentName);

	SphereComponent->SetupAttachment(GetDecal());
	
	SphereComponent->InitSphereRadius(50.f);
	SphereComponent->SetCollisionProfileName(TEXT("OverlapAll"));
	SphereComponent->SetCanEverAffectNavigation(false);
	SphereComponent->SetGenerateOverlapEvents(true);

	SphereComponent->OnComponentBeginOverlap.AddDynamic(this, &ABloodActor::OnOverlapBegin);
	SphereComponent->OnComponentEndOverlap.AddDynamic(this, &ABloodActor::OnOverlapEnd);
	
	InteractionWidgetClass = nullptr;
	InteractionWidgetInstance = nullptr;
}

void ABloodActor::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 오버랩된 액터가 로컬 플레이어의 폰인지 확인합니다.
	APawn* PlayerPawn = Cast<APawn>(OtherActor);
	if (PlayerPawn && PlayerPawn->IsLocallyControlled())
	{
		// 1. 위젯 클래스가 유효하고, 아직 위젯이 생성되지 않았는지 확인
		if (InteractionWidgetClass && !InteractionWidgetInstance)
		{
			// 2. 로컬 플레이어의 컨트롤러를 가져옵니다. 위젯은 컨트롤러를 통해 생성됩니다.
			APlayerController* PlayerController = Cast<APlayerController>(PlayerPawn->GetController());
			if (PlayerController)
			{
				// 3. 위젯을 생성합니다.
				InteractionWidgetInstance = CreateWidget<UUserWidget>(PlayerController, InteractionWidgetClass);
				if (InteractionWidgetInstance)
				{
					// 4. 뷰포트(화면)에 위젯을 추가합니다.
					InteractionWidgetInstance->AddToViewport();
					UE_LOG(LogTemp, Log, TEXT("Interaction widget created and shown for %s"), *GetName());
				}
			}
		}
	}
}

void ABloodActor::Initialize(const FString& InReplayFileName)
{
	ReplayFileName = InReplayFileName;
}

void ABloodActor::OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex)
{
	APawn* PlayerPawn = Cast<APawn>(OtherActor);
	if (PlayerPawn && PlayerPawn->IsLocallyControlled())
	{
		// 1. 현재 표시되고 있는 위젯이 있는지 확인
		if (InteractionWidgetInstance)
		{
			// 2. 뷰포트에서 위젯을 제거합니다.
			InteractionWidgetInstance->RemoveFromParent();
            
			// 3. 포인터를 nullptr로 만들어 다음 오버랩 시 새로 생성할 수 있도록 하고,
			//    메모리에서 해제(가비지 컬렉션)되도록 합니다.
			InteractionWidgetInstance = nullptr;
			UE_LOG(LogTemp, Log, TEXT("Interaction widget removed for %s"), *GetName());
		}
	}
}

void ABloodActor::Interact()
{
	if (UWorld* World = GetWorld())
	{
		if (UBloodStainSubsystem* Sub = World->GetGameInstance()->GetSubsystem<UBloodStainSubsystem>())
		{
			Sub->StartReplayFromFile(this, ReplayFileName);
		}
	}
}