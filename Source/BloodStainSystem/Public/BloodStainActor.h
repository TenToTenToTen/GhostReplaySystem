/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#pragma once

#include "CoreMinimal.h"
#include "OptionTypes.h"
#include "Engine/DecalActor.h"
#include "BloodStainActor.generated.h"

class USphereComponent;
class UPrimitiveComponent;
class UUserWidget;

/**
 * Demo Actor used for triggering replay
 */
UCLASS()
class BLOODSTAINSYSTEM_API ABloodStainActor : public ADecalActor
{
	GENERATED_BODY()

public:
	ABloodStainActor();

	void Initialize(const FString& InReplayFileName, const FString& InLevelName);
	
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	
	UFUNCTION()
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
	
	// Interaction Logic (e.g. called when the E key is pressed)
	UFUNCTION(Category = "BloodStainActor", BlueprintCallable)
	void Interact();

public:
	/** Replay Target File Name without Directory Path */
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category="BloodStainActor")
	FString ReplayFileName;

	/** Replay Target Level Name */
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Category="BloodStainActor")
	FString LevelName;

	/** Replay Playback Option */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="BloodStainActor")
	FBloodStainPlaybackOptions PlaybackOptions;

protected:
	/** Whether to allow multiple playback. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="BloodStainActor")
	uint8 bAllowMultiplePlayback : 1 = true;
	
	/** Last Played Playback Key. Use for Control Playing BloodStain */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="BloodStainActor")
	FGuid LastPlaybackKey;

	UPROPERTY()
	TObjectPtr<UUserWidget> InteractionWidgetInstance;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "BloodStainActor|UI")
	TSubclassOf<UUserWidget> InteractionWidgetClass;
	
	UPROPERTY(BlueprintReadOnly, Category = "BloodStainActor", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> SphereComponent;
	
private:
	static FName SphereComponentName;
};
