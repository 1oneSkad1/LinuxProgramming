// Optional PNG plot using the Codex-bundled native chart renderer.
// Set ARTIFACT_TOOL_MODULE to its absolute module URL when not installed locally.
import fs from 'node:fs/promises';
import path from 'node:path';
const {Presentation}=await import(process.env.ARTIFACT_TOOL_MODULE || '@oai/artifact-tool');
const directory=path.resolve(process.argv[2] || 'results/benchmark');
const lines=(await fs.readFile(path.join(directory,'comparison.csv'),'utf8')).trim().split(/\r?\n/);
const fields=lines[0].split(',');
const rows=lines.slice(1).map(line=>Object.fromEntries(line.split(',').map((v,i)=>[fields[i],v])));
const environment=JSON.parse(await fs.readFile(path.join(directory,'environment.json'),'utf8'));
const deck=Presentation.create({slideSize:{width:1000,height:640}});
const slide=deck.slides.add();slide.background.fill='#FFFFFF';
const title=slide.shapes.add({geometry:'textbox',position:{left:50,top:25,width:900,height:55},fill:'none',line:{fill:'none',width:0}});
title.text='Median execution time (lower is faster)';
title.text.style={fontSize:28,typeface:'Arial',color:'#222222'};
slide.charts.add('bar',{
    position:{left:60,top:105,width:880,height:420},
    categories:rows.map(r=>r.mode==='sequential'?'Sequential':`${r.workers} thread${r.workers==='1'?'':'s'}`),
    series:[{name:'Seconds',values:rows.map(r=>Number(r.median_seconds)),fill:'#2C5B80'}],
    barOptions:{direction:'column',grouping:'clustered',gapWidth:90},
    hasLegend:false,dataLabels:{showValue:true,position:'outEnd',numberFormatCode:'0.000'},
    yAxis:{title:'Seconds',numberFormatCode:'0.00',majorGridlines:{fill:'#DDDDDD',width:1,style:'solid'}},
});
const footer=slide.shapes.add({geometry:'textbox',position:{left:60,top:555,width:880,height:60},fill:'none',line:{fill:'none',width:0}});
footer.text=`${environment.files} files; ${environment.repeats} runs per condition after warm-up.\nWSL /mnt/c storage; caches retained. See comparison.csv for min/max and RSS.`;
footer.text.style={fontSize:18,typeface:'Arial',color:'#555555'};
const png=await deck.export({slide,format:'png',scale:1.5});
const target=path.join(directory,'comparison.png');
await fs.writeFile(target,new Uint8Array(await png.arrayBuffer()));
console.log(target);
