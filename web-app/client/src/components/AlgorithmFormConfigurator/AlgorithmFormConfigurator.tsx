import { useRouter } from 'next/router';
import React from 'react';
import FormFooter from '@components/AlgorithmFormConfigurator/FormFooter';
import FormHeader from '@components/AlgorithmFormConfigurator/FormHeader';
import useFormFactory from '@components/AlgorithmFormConfigurator/useFormFactory';
import PresetSelector from '@components/PresetSelector';
import WizardLayout from '@components/WizardLayout';
import { UsedPrimitivesType } from '@constants/formPrimitives';
import styles from './ConfigureAlgorithm.module.scss';

type QueryProps<T extends UsedPrimitivesType> = {
  primitive: T;
  fileID: string;
  formParams: { [key: string]: string | string[] | undefined };
};

const AlgorithmFormConfigurator = <T extends UsedPrimitivesType>({
  primitive,
  fileID,
  formParams,
}: QueryProps<T>) => {
  const router = useRouter();

  const {
    methods,
    entries,
    formPresets,
    fileNameLoading,
    changePreset,
    onSubmit,
  } = useFormFactory({
    fileID,
    primitive,
    formParams,
  });

  const numColumnContainer = `container${
    entries.length > 4 ? 'Over4' : 'Less4'
  }Inputs`;

  const containerOuter = entries.length > 4 ? 'bigContainer' : 'containerLess4Inputs'

  return (
    <WizardLayout header={FormHeader} footer={FormFooter(router, onSubmit)}>
      <div className={styles[containerOuter]}>
        <div className={styles[numColumnContainer]}>
          <PresetSelector
            presets={formPresets}
            isCustom={methods.formState.isDirty}
            changePreset={changePreset}
            isLoading={fileNameLoading}
          />
        </div>

        <div className={styles.line} />
        <div className={styles[numColumnContainer]}>{entries}</div>
      </div>
    </WizardLayout>
  );
};

export default AlgorithmFormConfigurator;
