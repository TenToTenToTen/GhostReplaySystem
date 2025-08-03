/*
* Copyright 2025 TenToTen, All Rights Reserved.
*/

#pragma once

#include "CoreMinimal.h"
#include "GhostData.h"
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

	virtual void BeginPlay() override;
	
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Initialize(const FString& InReplayFileName, const FString& InLevelName);
	
	UFUNCTION(BlueprintNativeEvent, Category="BloodStainActor")
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);
	
	UFUNCTION(BlueprintNativeEvent, Category="BloodStainActor")
	void OnOverlapEnd(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex);
	
	// Interaction Logic (e.g. called when the E key is pressed)
	UFUNCTION(BlueprintCallable, Category = "BloodStainActor")
	void Interact();

	UFUNCTION(Server, Reliable)
	void Server_Interact();
	
	UFUNCTION(Client, Reliable)
	void Client_ShowInteractionWidget();

	UFUNCTION(Client, Reliable)
	void Client_HideInteractionWidget();

	UFUNCTION(BlueprintCallable, Category = "BloodStainActor")
	bool GetHeaderData(FRecordHeaderData& OutRecordHeaderData);

public:
	/** Replay Target File Name without Directory Path */
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Replicated, Category="BloodStainActor")
	FString ReplayFileName;

	/** Replay Target Level Name */
	UPROPERTY(BlueprintReadWrite, VisibleAnywhere, Replicated, Category="BloodStainActor")
	FString LevelName;

	/** Replay Playback Option */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="BloodStainActor")
	FBloodStainPlaybackOptions PlaybackOptions;

protected:
	void StartReplay();
	
	/** Whether to allow multiple playback. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category="BloodStainActor")
	uint8 bAllowMultiplePlayback : 1 = true;
	
	/** Last Played Playback Key. Use for Control Playing BloodStain */
	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="BloodStainActor")
	FGuid LastPlaybackKey;

	UPROPERTY(BlueprintReadOnly, VisibleAnywhere, Category="BloodStainActor")
	TObjectPtr<UUserWidget> InteractionWidgetInstance;

	UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "BloodStainActor|UI")
	TSubclassOf<UUserWidget> InteractionWidgetClass;
	
	UPROPERTY(BlueprintReadOnly, Category = "BloodStainActor", meta=(AllowPrivateAccess = "true"))
	TObjectPtr<USphereComponent> SphereComponent;

	/** [SERVER-ONLY] currently approved to interact this bloodstain */
	UPROPERTY()
	TObjectPtr<APlayerController> InteractingPlayerController;
	
private:
	static FName SphereComponentName;
};
