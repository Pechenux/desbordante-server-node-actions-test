export type Badge = {
  label: string;
  style?: string;
};

export type OptionWithBadges = {
  label: string;
  value: string | number;
  badges?: Badge[];
};
