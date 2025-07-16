/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#pragma once

#include "OptionTypes.h"
#include "Engine/DecalActor.h"
#include "BloodActor.generated.h"

/**
 * BloodStianActor 혈흔 보여주고 상호작용 관련된 것 처리 해주는 Actor
 * BloodStainSubsystem을 통해서 Spawn 되고 Spawn할때 ReplayData의 경로를 전달 받아야하고
 * Spawn위치는 BloodStainSubSystem에 의해 정해질텐데 그 위치는 RecordComponent가 같이 전달해줘야할 것 같음.
 * 상호작용하면 Subsystem을 통해 Play함수 호출, 매개변수는 ReplayData 경로
 */
UCLASS()
class BLOODSTAINSYSTEM_API ABloodActor : public ADecalActor
{
	GENERATED_BODY()

public:
	ABloodActor();

public:
	void Initialize(const FString& InReplayFileName, const FString& InLevelName);
	
	// 상호작용 로직 (예: E 키를 눌렀을 때 호출)
	UFUNCTION(Category = Interaction, BlueprintCallable)
	void Interact();

private:
	UPROPERTY()
	TObjectPtr<UUserWidget> InteractionWidgetInstance;
	
	UPROPERTY(Category = Interaction, VisibleAnywhere, BlueprintReadOnly, meta=(AllowPrivateAccess = "true"))
	TObjectPtr<class USphereComponent> SphereComponent;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	
	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "UI")
	TSubclassOf<UUserWidget> InteractionWidgetClass;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BloodStain")
	FString ReplayFileName;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BloodStain")
	FString LevelName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="BloodStain")
	FBloodStainPlaybackOptions PlaybackOptions;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="BloodStain")
	bool bAllowMultiplePlayback = true;
	
private:
	/** Last Played Playback Key. Use for Control Playback */
	UPROPERTY()
	FGuid PlaybackKey;

	static FName SphereComponentName;
};
