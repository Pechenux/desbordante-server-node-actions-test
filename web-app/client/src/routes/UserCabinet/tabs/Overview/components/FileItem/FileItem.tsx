import { FC } from 'react';
import styles from './FileItem.module.scss';
import { getUser_user_datasets_data } from '@graphql/operations/queries/__generated__/getUser';
import FileIcon from '@assets/icons/file.svg?component';
import prettyBytes from 'pretty-bytes';

interface Props {
  data: getUser_user_datasets_data;
  percentage: number;
}

const FileItem: FC<Props> = ({
  data: { originalFileName, fileSize },
  percentage = 69,
}) => {
  return (
    <li className={styles.fileItem}>
      <div className={styles.iconContainer}>
        <FileIcon />
      </div>
      <div className={styles.middle}>
        <small className={styles.fileName}>{originalFileName}</small>
        <small className={styles.fileSize}>{prettyBytes(fileSize)}</small>
      </div>
      <p className={styles.percentage}>{(percentage * 100).toFixed(1)}%</p>
    </li>
  );
};

export default FileItem;
