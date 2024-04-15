import classNames from 'classnames';
import { useEffect, useState } from 'react';
import ListIcon from '@assets/icons/list.svg?component';
import Button from '@components/Button';
import NavBar from '@components/NavBar';
import { useAuthContext } from '@hooks/useAuthContext';
import useModal from '@hooks/useModal';
import styles from './Header.module.scss';
import Link from 'next/link';

const Header = () => {
  const { user, signOut } = useAuthContext();
  const { open: openLogInModal } = useModal('AUTH.LOG_IN');
  const { open: openSignUpModal } = useModal('AUTH.SIGN_UP');

  const [headerBackground, setHeaderBackground] = useState(false);

  useEffect(() => {
    const checkScroll = () => {
      setHeaderBackground(window.pageYOffset > 100);
    };

    window.addEventListener('scroll', checkScroll);
    return () => {
      window.removeEventListener('scroll', checkScroll);
    };
  }, []);

  return (
    <header
      className={classNames(
        styles.header,
        headerBackground && styles.background
      )}
    >
      <NavBar />
      <Button
        variant="secondary"
        size="sm"
        aria-label="open menu"
        icon={<ListIcon />}
        className={styles.menu}
      />
      <div className={styles.auth}>
        {user?.name ? (
          <>
            <p>
              Welcome,{' '}
              <Link className={styles.userCabinetLink} href="/me">
                {user.name}
              </Link>
            </p>
            {!user.isVerified && (
              <Button
                variant="secondary"
                size="sm"
                onClick={() => openSignUpModal({})}
              >
                Verify Email
              </Button>
            )}
            <Button variant="secondary-danger" size="sm" onClick={signOut}>
              Log Out
            </Button>
          </>
        ) : (
          <>
            <Button
              variant="secondary"
              size="sm"
              onClick={() => openLogInModal({})}
            >
              Log In
            </Button>
            <Button
              variant="gradient"
              size="sm"
              onClick={() => openSignUpModal({})}
            >
              Sign Up
            </Button>
          </>
        )}
      </div>
    </header>
  );
};

export default Header;
