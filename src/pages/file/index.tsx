import { NextPage } from 'next'
import Head from 'next/head'
import Image from 'next/image'
import styles from '../../styles/Home.module.css'
import { GitHub } from '@mui/icons-material'
import Script from 'next/script'

const Home: NextPage = () => {
  return (
    <div className={styles.container}>
      <Head>
        <title>TS-Live!</title>
        <meta
          name='description'
          content='TS-Live! experimental mirakurun client for browser'
        />
        <link rel='icon' href='/favicon.ico' />
        <Script
          src='https://www.googletagmanager.com/gtag/js?id=G-SR7L1XYNV0'
          strategy='afterInteractive'
        />
        <Script id='google-analytics' strategy='afterInteractive'>
          {`
            window.dataLayer = window.dataLayer || [];
            function gtag(){window.dataLayer.push(arguments);}
            gtag('js', new Date());

            gtag('config', 'G-SR7L1XYNV0');
          `}
        </Script>
      </Head>

      <main className={styles.main}>Comming soon...</main>

      <footer className={styles.footer}></footer>
    </div>
  )
}

export default Home
