#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ArenaTypes.h"
#include "ArenaCharacter.generated.h"

class UCameraComponent;
class UPointLightComponent;
class UStaticMeshComponent;
class UArenaMovementComponent;
struct FInputActionInstance;
struct FInputActionValue;

/** What the third-person animation needs from the character each frame. */
struct FArenaPoseInput
{
	FVector Velocity = FVector::ZeroVector;
	bool bOnGround = true;
	bool bCrouched = false;
	/** Degrees, positive up. */
	float AimPitch = 0.f;
	/** Gun and left-hand grip in the body mesh's component space. */
	FTransform Gun = FTransform::Identity;
	FVector LeftGrip = FVector::ZeroVector;
	bool bHoldingGun = false;
	bool bLeftGrip = false;
};

/** Fakes movement for the body animation (the showcase poses characters with it). */
struct FArenaPoseOverride
{
	bool bEnabled = false;
	/** Relative to the character: X forward, Y right. */
	FVector LocalVelocity = FVector::ZeroVector;
	bool bInAir = false;
	bool bCrouched = false;
	float AimPitch = 0.f;
};

UCLASS()
class ARENA_API AArenaCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	AArenaCharacter(const FObjectInitializer& ObjectInitializer);

	static constexpr int32 MaxHealth = 200;   // Mega health can go over 100 and ticks down.
	static constexpr int32 SpawnHealth = 125; // Quake 3 spawn health, ticks down to 100.
	static constexpr int32 MaxArmor = 200;

	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;

	UArenaMovementComponent* GetArenaMovement() const;
	UCameraComponent* GetCamera() const { return Camera; }

	int32 GetHealth() const { return Health; }
	int32 GetArmor() const { return Armor; }
	bool IsDead() const { return bDead; }
	EArenaWeapon GetCurrentWeapon() const { return CurrentWeapon; }
	int32 GetAmmo(EArenaWeapon Weapon) const { return Ammo.IsValidIndex((int32)Weapon) ? Ammo[(int32)Weapon] : 0; }
	bool HasWeapon(EArenaWeapon Weapon) const { return (OwnedWeapons & (1 << (int32)Weapon)) != 0; }

	/**
	 * Server: applies damage with Quake-style armor absorption and knockback.
	 * SourceLocation (shooter or explosion) drives the victim's damage direction indicator.
	 */
	void ApplyArenaDamage(float Damage, AController* InstigatorController, const FVector& Knockback, EArenaWeapon Weapon, const FVector& SourceLocation);

	/** Server: pickup helpers. Return false if the pickup would be wasted. */
	bool GiveHealth(int32 Amount, int32 Cap);
	bool GiveArmor(int32 Amount);
	bool GiveWeapon(EArenaWeapon Weapon, int32 AmmoAmount);

	/** Adds velocity on server and owning client (rocket jumps, jump pads). */
	void AddKnockback(const FVector& Impulse);

	/** Tip of whichever gun this machine renders for this pawn. */
	FVector GetMuzzleLocation() const;

	/** Cosmetic: muzzle flash, gun recoil and spin-up. Runs wherever a shot is seen. */
	void PlayFireEffects();

	FArenaPoseInput GetPoseInput() const;
	FArenaPoseOverride PoseOverride;

	/** True only on the machine of the human playing this pawn (not for bots on the server). */
	bool IsLocalPlayerView() const { return IsLocallyControlled() && IsPlayerControlled(); }

	// Used by bots, which drive the pawn through the same actions a player has.
	void PullTrigger() { TryFire(); }
	void EquipWeapon(EArenaWeapon Weapon) { SelectWeapon(Weapon); }
	bool CanFire(EArenaWeapon Weapon) const;

	/** Paints the body in the owner's player colour. */
	void UpdateColors();
	void SetBodyColor(const FLinearColor& Color);

	/** Server: kills the pawn outright (showcase, console). */
	void Kill() { Die(nullptr, EArenaWeapon::Count); }

protected:
	virtual void BeginPlay() override;
	virtual bool CanJumpInternal_Implementation() const override;

	// Input
	void OnMoveForward(const FInputActionValue& Value);
	void OnMoveRight(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnJumpStarted();
	void OnJumpHeld();
	void OnJumpReleased();
	void OnJumpWheel();
	void OnCrouchStarted();
	void OnCrouchReleased();
	void OnFireHeld();
	void OnSelectWeaponAction(const FInputActionInstance& Instance);
	void OnNextWeapon();
	void OnLastWeapon();

	void TryFire();
	void SelectWeapon(EArenaWeapon Weapon);
	void FireHitscan(const FVector& Origin, const FVector& Dir, EArenaWeapon Weapon, int32 Seed);
	void FireProjectile(const FVector& Origin, const FVector& Dir, EArenaWeapon Weapon);
	void Die(AController* Killer, EArenaWeapon Weapon);
	void UpdateWeaponVisuals();

	UFUNCTION(Server, Reliable)
	void ServerFire(FVector_NetQuantize Origin, FVector_NetQuantizeNormal Dir, int32 Seed);

	UFUNCTION(Server, Reliable)
	void ServerSelectWeapon(EArenaWeapon Weapon);

	UFUNCTION(Client, Reliable)
	void ClientAddKnockback(FVector_NetQuantize Impulse);

	UFUNCTION()
	void OnRep_Dead();

	UFUNCTION()
	void OnRep_Health(int32 OldHealth);

	void UpdateMovementSounds(float DeltaSeconds);
	void UpdateWorldGun(float DeltaSeconds);
	void UpdateViewModel(float DeltaSeconds);
	void UpdateGunEffects(float DeltaSeconds);
	void UpdateDeathCamera(float DeltaSeconds);
	void AttachGunParts(UStaticMeshComponent* Gun, UStaticMeshComponent* Spin, UStaticMeshComponent* Flash);

	UFUNCTION()
	void OnRep_CurrentWeapon();

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UCameraComponent> Camera;

	/** Third-person gun, seen by everyone else, held by the body's hand IK. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> WorldGunMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> WorldGunSpin;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> WorldFlash;

	/** First-person gun, seen only by the owner. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ViewGunMesh;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ViewGunSpin;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> ViewFlash;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UPointLightComponent> FlashLight;

	UPROPERTY(ReplicatedUsing = OnRep_Health)
	int32 Health = SpawnHealth;

	UPROPERTY(Replicated)
	int32 Armor = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Dead)
	bool bDead = false;

	UPROPERTY(ReplicatedUsing = OnRep_CurrentWeapon)
	EArenaWeapon CurrentWeapon = EArenaWeapon::MachineGun;

	UPROPERTY(Replicated)
	TArray<int32> Ammo;

	UPROPERTY(Replicated)
	uint8 OwnedWeapons = 0;

private:
	EArenaWeapon LastWeapon = EArenaWeapon::MachineGun;
	float NextFireTime = 0.f;       // Local (client) refire gate.
	float ServerNextFireTime = 0.f; // Authoritative refire gate.
	float TickDownAccumulator = 0.f;
	float ViewKick = 0.f;
	bool bJumpHeld = false;

	// Cosmetic gun state.
	FTransform GunFrame = FTransform::Identity; // Third-person gun, body component space.
	float BodyKick = 0.f;
	float FlashTime = 0.f;
	float LastShotTime = -100.f;
	float SpinSpeed = 0.f;
	float SpinAngle = 0.f;
	float GunCrouch = 0.f;
	FVector LeftGrip = FVector::ZeroVector;
	bool bHasLeftGrip = false;

	// First-person view model motion.
	float BobPhase = 0.f;
	float BobAmount = 0.f;
	float RaiseAlpha = 1.f;
	float LandDip = 0.f;
	FRotator Sway = FRotator::ZeroRotator;
	FRotator LastViewRotation = FRotator::ZeroRotator;

	// Cosmetic movement-sound state, tracked on every machine.
	bool bWasOnGround = true;
	float LastVelocityZ = 0.f;
	float FootstepTimer = 0.f;
	float LastPainTime = -100.f;
};
