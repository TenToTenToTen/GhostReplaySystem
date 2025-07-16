/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#pragma once

#include "CoreMinimal.h"
#include "OptionTypes.h"
#include "Engine/DecalActor.h"
#include "BloodActor.generated.h"

class USphereComponent;
class UPrimitiveComponent;
class UUserWidget;

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

	void Initialize(const FString& InReplayFileName, const FString& InLevelName);
	
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	
	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
	
	// Interaction Logic (e.g. called when the E key is pressed)
	UFUNCTION(Category = "BloodActor", BlueprintCallable)
	void Interact();

public:	
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category="BloodActor")
	FString ReplayFileName;
	
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category="BloodActor")
	FString LevelName;

	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="BloodActor")
	FBloodStainPlaybackOptions PlaybackOptions;

protected:
	UPROPERTY()
	TObjectPtr<UUserWidget> InteractionWidgetInstance;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "BloodActor|UI")
	TSubclassOf<UUserWidget> InteractionWidgetClass;
	
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="BloodActor")
	bool bAllowMultiplePlayback = true;
	
	/** Last Played Playback Key. Use for Control Playing BloodStain */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="BloodActor")
	FGuid LastPlaybackKey;
	
	UPROPERTY(BlueprintReadOnly, Category = "BloodActor", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> SphereComponent;
	
private:
	static FName SphereComponentName;
};
