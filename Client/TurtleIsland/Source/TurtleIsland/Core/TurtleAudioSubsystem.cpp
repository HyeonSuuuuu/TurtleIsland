#include "Core/TurtleAudioSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Widgets/SWindow.h"

namespace
{
	struct FSoundDef
	{
		ETurtleSound Sound;
		const TCHAR* Asset;   // /Game/Audio/SFX/<Asset> or <Asset>_01.._NN
		int32 VariantCount;   // 0 = single asset without suffix
		float Volume;
	};

	const FSoundDef SoundDefs[] = {
		{ETurtleSound::BasicSwing, TEXT("SFX_Swing"), 2, 0.8f},
		{ETurtleSound::SkillQ, TEXT("SFX_SkillQ"), 3, 0.9f},
		{ETurtleSound::SkillECast, TEXT("SFX_SkillECast"), 0, 0.5f},
		{ETurtleSound::SkillEImpact, TEXT("SFX_SkillEImpact"), 3, 1.f},
		{ETurtleSound::HitMonster, TEXT("SFX_HitMonster"), 5, 0.8f},
		{ETurtleSound::HitPlayer, TEXT("SFX_HitPlayer"), 3, 0.9f},
		{ETurtleSound::MonsterAttack, TEXT("SFX_MonsterAttack"), 4, 0.7f},
		{ETurtleSound::MonsterDeath, TEXT("SFX_MonsterDeath"), 3, 0.9f},
		{ETurtleSound::PlayerDeath, TEXT("SFX_PlayerDeath"), 0, 0.6f},
		{ETurtleSound::Respawn, TEXT("SFX_Respawn"), 0, 0.5f},
		{ETurtleSound::LevelUp, TEXT("SFX_LevelUp"), 0, 0.6f},
		{ETurtleSound::Potion, TEXT("SFX_Potion"), 3, 0.7f},
		{ETurtleSound::Purchase, TEXT("SFX_Purchase"), 2, 0.8f},
		{ETurtleSound::QuestAccept, TEXT("SFX_QuestAccept"), 0, 0.7f},
		{ETurtleSound::QuestReady, TEXT("SFX_QuestReady"), 0, 0.7f},
		{ETurtleSound::QuestComplete, TEXT("SFX_QuestComplete"), 0, 0.6f},
		{ETurtleSound::NpcReply, TEXT("SFX_NpcReply"), 0, 0.5f},
		{ETurtleSound::UIClick, TEXT("SFX_UIClick"), 1, 0.5f},
		{ETurtleSound::WindowOpen, TEXT("SFX_WindowOpen"), 0, 0.6f},
		{ETurtleSound::WindowClose, TEXT("SFX_WindowClose"), 0, 0.6f},
	};
}

// Plays the click sound for mouse presses that land on an enabled button in this game instance's window.
class FTurtleClickSoundProcessor : public IInputProcessor
{
public:
	explicit FTurtleClickSoundProcessor(UTurtleAudioSubsystem* InOwner) : Owner(InOwner) {}
	virtual void Tick(const float, FSlateApplication&, TSharedRef<ICursor>) override {}
	virtual bool HandleMouseButtonDownEvent(FSlateApplication& Slate, const FPointerEvent& Event) override
	{
		UTurtleAudioSubsystem* Audio = Owner.Get();
		if (!Audio) return false;
		const FWidgetPath Path = Slate.LocateWindowUnderMouse(Event.GetScreenSpacePosition(), Slate.GetInteractiveTopLevelWindows());
		if (!Path.IsValid() || !Audio->IsGameWindow(Path.GetWindow())) return false;
		static const FName ButtonType(TEXT("SButton"));
		for (int32 i = Path.Widgets.Num() - 1; i >= 0; --i)
			if (Path.Widgets[i].Widget->GetType() == ButtonType && Path.Widgets[i].Widget->IsEnabled())
			{
				Audio->PlaySound(ETurtleSound::UIClick, nullptr);
				break;
			}
		return false; // never consume the click
	}
	virtual const TCHAR* GetDebugName() const override { return TEXT("TurtleClickSound"); }
private:
	TWeakObjectPtr<UTurtleAudioSubsystem> Owner;
};

void UTurtleAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	VariantIndices.SetNum(static_cast<int32>(ETurtleSound::Count));
	for (const FSoundDef& Def : SoundDefs)
	{
		for (int32 i = 0; i < FMath::Max(Def.VariantCount, 1); ++i)
		{
			const FString Name = Def.VariantCount ? FString::Printf(TEXT("%s_%02d"), Def.Asset, i + 1) : FString(Def.Asset);
			if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/Audio/SFX/%s.%s"), *Name, *Name)))
				VariantIndices[static_cast<int32>(Def.Sound)].Add(Variants.Add(Sound));
			else
				UE_LOG(LogTemp, Warning, TEXT("[Audio] Missing sound /Game/Audio/SFX/%s"), *Name);
		}
	}
	WorldAttenuation = NewObject<USoundAttenuation>(this);
	WorldAttenuation->Attenuation.bAttenuate = true;
	WorldAttenuation->Attenuation.bSpatialize = true;
	WorldAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	WorldAttenuation->Attenuation.AttenuationShapeExtents = FVector(600.f, 0.f, 0.f); // full volume within 6m
	WorldAttenuation->Attenuation.FalloffDistance = 2400.f;

	if (FSlateApplication::IsInitialized())
	{
		ClickProcessor = MakeShared<FTurtleClickSoundProcessor>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(ClickProcessor);
	}
}

void UTurtleAudioSubsystem::Deinitialize()
{
	if (ClickProcessor && FSlateApplication::IsInitialized()) FSlateApplication::Get().UnregisterInputPreProcessor(ClickProcessor);
	ClickProcessor.Reset();
	Super::Deinitialize();
}

bool UTurtleAudioSubsystem::IsGameWindow(const TSharedPtr<SWindow>& Window) const
{
	const UGameViewportClient* Viewport = GetGameInstance() ? GetGameInstance()->GetGameViewportClient() : nullptr;
	return Window && Viewport && Viewport->GetWindow() == Window;
}

void UTurtleAudioSubsystem::Play(const UObject* WorldContext, ETurtleSound Sound, const FVector* Location)
{
	const UWorld* World = WorldContext ? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (UTurtleAudioSubsystem* Audio = World && World->GetGameInstance() ? World->GetGameInstance()->GetSubsystem<UTurtleAudioSubsystem>() : nullptr)
		Audio->PlaySound(Sound, Location);
}

void UTurtleAudioSubsystem::PlaySound(ETurtleSound Sound, const FVector* Location)
{
	const TArray<int32>& Indices = VariantIndices[static_cast<int32>(Sound)];
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (Indices.IsEmpty() || !World || World->IsNetMode(NM_DedicatedServer)) return;
	float Volume = 1.f;
	for (const FSoundDef& Def : SoundDefs) if (Def.Sound == Sound) Volume = Def.Volume;
	USoundBase* Chosen = Variants[Indices[FMath::RandHelper(Indices.Num())]];
	const float Pitch = FMath::FRandRange(0.94f, 1.06f);
	if (Location) UGameplayStatics::PlaySoundAtLocation(World, Chosen, *Location, Volume, Pitch, 0.f, WorldAttenuation);
	else UGameplayStatics::PlaySound2D(World, Chosen, Volume, Pitch);
}
