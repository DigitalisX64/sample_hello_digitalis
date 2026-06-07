import './App.css'

// A deliberately static page (no animation, no remote images) so the Digitalis
// suite's ScreenshotTest can validate the Lynx-rendered output deterministically.
export function App() {
  return (
    <view className='Root'>
      <view className='Card'>
        <text className='Title'>Hello Lynx</text>
        <text className='Subtitle'>ReactLynx on Digitalis</text>
      </view>
    </view>
  )
}
