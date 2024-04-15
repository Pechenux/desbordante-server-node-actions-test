import dynamic from 'next/dynamic';
import { ComponentType, ReactNode } from 'react';
import ChartIcon from '@assets/icons/chart.svg?component';
import GearIcon from '@assets/icons/gear.svg?component';
import FileIcon from '@assets/icons/file.svg?component';
import UserIcon from '@assets/icons/user.svg?component';

export type TabConfig = {
  pathname: string;
  label: string;
  icon: ReactNode;
  component: ComponentType;
};

const OverviewComponent = dynamic(() => import('./Overview'), {
  ssr: false,
});


const tabs: TabConfig[] = [
  {
    pathname: 'overview',
    label: 'Overview',
    icon: <ChartIcon />,
    component: OverviewComponent,
  },
];

export default tabs;