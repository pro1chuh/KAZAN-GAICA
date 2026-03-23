class AdvancedBot:
    REVOLVER_RANGE = 320.0
    UZI_RANGE = 260.0
    BULLET_DANGER_DIST = 140.0

    def __init__(self):
        self.aim_lead = 4  # Number of iterations for aiming lead
        self.stuck_timer_threshold = 8  # Threshold for stuck timer
        self.uzi_shoot_threshold_dot = 0.45  # UZI shoot threshold dot
        self.strafe_interval = 18  # Interval for strafing
        self.enemy_speed_tracking_smoothing = 0.5  # Smoothing factor for enemy speed tracking
        self.bullet_dodge_future_ticks = 6  # Future ticks for bullet dodge

    # Additional methods and functionality for AdvancedBot can be added here
