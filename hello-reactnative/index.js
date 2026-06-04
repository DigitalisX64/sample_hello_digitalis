/**
 * hello-reactnative — minimal React Native + Hermes sample for Digitalis.
 *
 * NetEase Cloud Music's React Native login screen runs on Hermes under Berberis
 * translation. Hermes routes number/string parsing (parseInt, Number, JSON,
 * RegExp) through bionic libc (strtoull/strtoumax/strtod), which is where a
 * worker thread was observed hanging on device. This sample exercises that exact
 * JS surface on app start and renders the result, so the sample suite has
 * end-to-end React-Native + Hermes coverage of the translated parsing path.
 */
import React from 'react';
import {AppRegistry, Text, View, StyleSheet} from 'react-native';

function exerciseParsing() {
  let acc = 0;
  // parseInt / Number across bases and forms -> bionic strtoull/strtoumax.
  for (let i = 0; i < 5000; i++) {
    acc += parseInt('0', 16);
    acc += parseInt(String(i), 10);
    acc += Number('0x' + (i & 0xff).toString(16));
    acc += parseFloat('3.14');
  }
  // Hermes RegExp engine.
  const re = /(\d+)-(\w+)/g;
  let matches = 0;
  let m;
  while ((m = re.exec('12-ab 34-cd 56-ef 0-zz')) !== null) matches++;
  // JSON number tokenization.
  const obj = JSON.parse('{"a":0,"b":123,"c":[1,2,3],"d":"0"}');
  return {acc, matches, json: obj.b};
}

function App() {
  const r = exerciseParsing();
  const ok = r.matches === 4 && r.json === 123 && Number.isFinite(r.acc);
  return (
    <View style={styles.container}>
      <Text style={styles.title}>hello-reactnative</Text>
      <Text style={styles.line} testID="rn_result">
        {`acc=${r.acc} matches=${r.matches} json=${r.json}`}
      </Text>
      <Text style={ok ? styles.ok : styles.fail} testID="rn_status">
        {ok ? 'RN+Hermes parsing OK' : 'RN+Hermes parsing FAILED'}
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  container: {flex: 1, justifyContent: 'center', alignItems: 'center', backgroundColor: '#ffffff'},
  title: {fontSize: 28, fontWeight: 'bold', color: '#cc0000', marginBottom: 16},
  line: {fontSize: 16, color: '#333333', marginBottom: 12},
  ok: {fontSize: 20, color: '#0a7d00', fontWeight: 'bold'},
  fail: {fontSize: 20, color: '#cc0000', fontWeight: 'bold'},
});

AppRegistry.registerComponent('hello-reactnative', () => App);
