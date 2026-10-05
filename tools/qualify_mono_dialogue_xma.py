"""Qualify the observed mono SNU stream without changing original audio bytes."""
import array
import csv
import importlib.util
import json
import os
import sys
from pathlib import Path
import qualify_menu_xma as q
from inspect_assets import inspect_snu

PROFILES=(
    dict(source='Simpsons Game, The (USA)/audiostreams/hr_xxx_0/d_wchr_xxx_0000b30.exa.snu',
         sha256='7c83691c18432764e22e7da33868817c0a1cd82e656afdd03be6f84edc1272bc',frames=177636,blocks=35,
         first='54dbd898bdadb5e77a11efd94671c3792f8070d554067d677fbdaff1bb86570d',directory='mono-dialogue-xma-wchr-b30'),
    dict(source='Simpsons Game, The (USA)/audiostreams/hr_xxx_0/d_wchr_xxx_0000b31.exa.snu',
         sha256='202927e7eb530e077db5e1543bfde9d654cc18356964c9a0d67e822c6218aa62',frames=141793,blocks=28,
         first='a2ffa6c1b20195174f56255e7e04e1add6cf887386993a691c8e7420b583f499',directory='mono-dialogue-xma'),
    dict(source='Simpsons Game, The (USA)/audiostreams/hr_xxx_0/d_wchr_xxx_0000b32.exa.snu',
         sha256='61a1d3397440cc9a48e1a9eb0d2c33db3e13db16cdaedc0f5382a3ab23a54bcc',frames=152410,blocks=30,
         first='a5fc8ea790438b51c858902f8c400e7b0de9c99c9e954c424d4035e1afc22621',directory='mono-dialogue-xma-wchr-b32'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0000b1e.exa.snu',
         sha256='16a44fbc062159fdc1bfa19e4fd46a95115cc55b7cc67c9eb3c4cd531da01740',frames=96735,blocks=19,
         first='ecd8171f39f12b5a53b5afae32c5c9f63bda16af8bb4e971e1098859042f4782',directory='mono-dialogue-xma-homer'),
    dict(source='Simpsons Game, The (USA)/audiostreams/hr_xxx_0/d_wchr_xxx_0000b34.exa.snu',
         sha256='dbea569e4596a98a4b4181eaca06e076c41bff469992b233243300af5c5a5189',frames=83715,blocks=17,
         first='29650d602787e2b55f7ffd4d6f3f8ffc89834951930cf840e0efd70fe247d0ff',directory='mono-dialogue-xma-wchr-b34'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e65.exa.snu',
         sha256='ee6bea761488b1452d4e9153e56f882cab120432d01ef7bbfed83f538a34a0a0',frames=102678,blocks=21,
         first='aaf94d73e2b552b1931a403ce7c0f9024ad51eca29059c0ee8c8b930642307bd',directory='mono-dialogue-xma-homer-3e65'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e62.exa.snu',
         sha256='88b36c4bc9b09c28785761a9e330c9ff7b3231f68f18ae5045f6fe27c5570099',frames=102088,blocks=21,
         first='b141a42cee2048612ff2ef24d24a7bac89e23c40b2d7670cba084bcc89d09657',directory='mono-dialogue-xma-homer-3e62'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e63.exa.snu',
         sha256='0a1216cee269e8d4f7c4fedab1baef87bfba5ecc70cff0f5e216a1c9cb8c1d7c',frames=110095,blocks=22,
         first='20a3505b052b015a9674c6a077a9a78c93521206a3160e1d8c0a42782e3389d4',directory='mono-dialogue-xma-homer-3e63'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e6d.exa.snu',
         sha256='e6e493405581847a401788fec99fd2887f1bd9d898e6b91fb5b456cfc95236e9',frames=97349,blocks=20,
         first='7906e3d93c17012c1e3bea8ba3322a265e7d0b1dd37b7167dbf5e1af4d49ef7e',directory='mono-dialogue-xma-homer-3e6d'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e6a.exa.snu',
         sha256='7eb1c62b82ad4cb12d67a6180de43f8f5e3c246f7a57cfa0a2f72a42fd88ccec',frames=143401,blocks=29,
         first='f96fd80cf55fc37afc61f469c88dc463b82f3ccfe74b7c3eedcdfeaaa75723ed',directory='mono-dialogue-xma-homer-3e6a'),
    dict(source='Simpsons Game, The (USA)/audiostreams/hr_xxx_0/d_wchr_xxx_0000b33.exa.snu',
         sha256='596912ec7ea9206d266a7988ac4c8d7a3a299d7141ae38224765ad28f1f7a73c',frames=180439,blocks=36,
         first='18b69d3092aa57f67d92da6f96571d92322007b9ddc7c478ef6d9dc6550b88b0',directory='mono-dialogue-xma-wchr-b33'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e64.exa.snu',
         sha256='1c6f6a7d92359fa8c68ebac95369672bca002160ba7dd6d66ce4f7a0a0e67560',frames=122154,blocks=24,
         first='3eba8851e92f6efeea0d3befa63f3aab502fb6bd8ada8821659e52242d2ee986',directory='mono-dialogue-xma-homer-3e64'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e66.exa.snu',
         sha256='c2084fbd2f1fa5e4314495ef4c305b6ed1fa322145f9a7a3d59ca44ddf8b4eda',frames=97095,blocks=20,
         first='24d291fcf22e3ba541e14b5211e8e9eb87994fa62006a3d6e08cfd31e1971e00',directory='mono-dialogue-xma-homer-3e66'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e67.exa.snu',
         sha256='ab106f2d2d4ef5824566909d038f6e961532f1f795dc5c7f155a66fbcaaea750',frames=105121,blocks=21,
         first='c0cb71f4c0ed13ff98e015375b05edf981d3926c9f80037efd6c75d6130e212d',directory='mono-dialogue-xma-homer-3e67'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e68.exa.snu',
         sha256='70c5b1204118eadcc97472b8bcc61080227ca393044dd52415cb397b1d0d0d39',frames=124472,blocks=25,
         first='0fb28ed0c0c88abea57da8ef08eb476a437bb3533003fbd676eea4ca8f908954',directory='mono-dialogue-xma-homer-3e68'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e69.exa.snu',
         sha256='3c6690b96dca4a3faa671c8b3f8bb17d8a32084fbf67ec4995798eebd35e2b1d',frames=142993,blocks=29,
         first='8fa0f1937ffa708d67fbd03f110ffe01e42bc127417bdbcb54254e90719e1daa',directory='mono-dialogue-xma-homer-3e69'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e6b.exa.snu',
         sha256='bb7758cb74a066ae98f023f133e5d2cd15bfb0a0518076eb761b89844c214e1c',frames=127226,blocks=25,
         first='3899a955783ef6ff70f33cbe6f9afa7695f33d1c692cd55142f9922c55874017',directory='mono-dialogue-xma-homer-3e6b'),
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0003e6c.exa.snu',
         sha256='d30435b930a66ab0062c263058ff7195a1942bea652d124836e4c0cd03784884',frames=133474,blocks=27,
         first='45a6d411de76118f6ea4499bf09eb6beb1f58a2fb99547807766b4ae3d7eefe3',directory='mono-dialogue-xma-homer-3e6c'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b23.exa.snu',
         sha256='1b6e9eb7c24aacb5ff2e3e76a88276a6d91aec2e60682ebb23005a4b46879935',frames=128658,blocks=26,
         first='3673ef5c91003f3f8282a060e361b45c5fb730c93478a1712a8082936dc7be73',directory='mono-dialogue-xma-chcb-b23'),
    dict(source='Simpsons Game, The (USA)/audiostreams/ls_xxx_0/d_nels_xxx_000683f.exa.snu',
         sha256='33f9a1e68c75b43fe9794d623fa6aea1c22c792465a4946a22d41a2f61a80fd0',frames=49348,blocks=10,
         first='832312dc8fc08e98f2bc1550bb1ce9d5144ca3c28d31d55defd81ffa5793cadc',directory='mono-dialogue-xma-nelson-683f'),
    # Related chocolate-rabbit story and reaction variants reached on this route.
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b1f.exa.snu',
         sha256='a478404c961277b855c370ef33134bc9bc6f45b456076386ec0dd68d4b54093d',frames=96000,blocks=19,
         first='7f7fa9ada6f8c21d5e9054d9b36ac2b8147b5417c24487b404d484e39877e9bd',directory='mono-dialogue-xma-chcb-b1f'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b20.exa.snu',
         sha256='a0bed2a65dc3e626572b55f4496889d11a7f5f0146daa29a9966f237d2286d88',frames=94208,blocks=19,
         first='2e4d9e738ce7b59a853c94c7cce1ec7e9a1375eab5366147d124a35898350d36',directory='mono-dialogue-xma-chcb-b20'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b21.exa.snu',
         sha256='d1c13d19a81690b588ec3597d92e74bf48c10dc2bf0eef42198bd9a7e21e7637',frames=88064,blocks=18,
         first='714223ad560ee047ab9c7deda846bc0b02c965e1e5f177e71d432ffc6c67e3c4',directory='mono-dialogue-xma-chcb-b21'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b22.exa.snu',
         sha256='899f31a3080ba26cf61166b32406dc601cb50e8ca76be6f09471199e77eb46b3',frames=96256,blocks=19,
         first='5276219ccf700aea1fbc60a2d41b437e177c2683bbe2ff6e5dfbb10857cd2d23',directory='mono-dialogue-xma-chcb-b22'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b24.exa.snu',
         sha256='efd3d80bf84d14951f2d3246b1be5e51b64c17108fbaf2a21e3399341134ae90',frames=189856,blocks=38,
         first='bf85559e473300f3b19e4b3c0f812d844e346a44e05dece0283f088d054298c7',directory='mono-dialogue-xma-chcb-b24'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b25.exa.snu',
         sha256='cce092fb62bbebecc6ac9b2880189ac94deec31177287a567e0b7faa3db07509',frames=86528,blocks=17,
         first='63b149c9a888ce6bc66e1dc49630dccffad080549ab1a6b79d858d9173133443',directory='mono-dialogue-xma-chcb-b25'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b26.exa.snu',
         sha256='449425118fe27c66c2d967068a0ca95ecded46c5b83a64215031d60beacaa97e',frames=85248,blocks=17,
         first='5a016fb58a06fdcc1f4b5d1a5277568011e8d152538fb0974305b675308c55e6',directory='mono-dialogue-xma-chcb-b26'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b27.exa.snu',
         sha256='4ddaa40bdf2de9deff8bb047d8c0789a966f41dd41bd77570744765b885c05fb',frames=63232,blocks=13,
         first='025a390ef07fd884b0ec0ca380e149b4d5e35f2531e631dd0ddbc9f19ee33e0a',directory='mono-dialogue-xma-chcb-b27'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b28.exa.snu',
         sha256='881ea1afee0f53f5dd18b8cc78af466ea1d75e13c0f5c45ffd600f7e125e0e5c',frames=90036,blocks=18,
         first='e98a70dc9ebfd4e687ce761c12688c86a8c98869bcfce90356bb0975d1c6d425',directory='mono-dialogue-xma-chcb-b28'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b29.exa.snu',
         sha256='5629b575f64ec3be20465c21fa7086c3b53ceba3eaa6d48687ba6d724531140d',frames=59520,blocks=12,
         first='affdc6d6ba218e48c0db55b9a956ca23330ec7f791904fb15b62e2eca9da83d1',directory='mono-dialogue-xma-chcb-b29'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b2a.exa.snu',
         sha256='2eab28918bc3153e15af5d55c2c509155275cb52eba687f33a6574628d10b6ea',frames=144057,blocks=29,
         first='5d48624c1a7e6252b16e585b2a4483a5f8aab8406becacac0934037c1d66a33d',directory='mono-dialogue-xma-chcb-b2a'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b2b.exa.snu',
         sha256='9a7aac625ef82bb88696c71a627a523680aca03c0c2215276a59114c40ea443c',frames=114688,blocks=23,
         first='7a1792275e4da6ade9b2ecce90283c27c5100f54285f8e94256417abd36799f4',directory='mono-dialogue-xma-chcb-b2b'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b2c.exa.snu',
         sha256='5c3e230d93fc27356bce178999e3208153de12a18a29c128b99d081ab5af6573',frames=209440,blocks=41,
         first='bf8b32ce3606f11114260c8eea9f9bfb8dc819afb3902411cff4463929204c80',directory='mono-dialogue-xma-chcb-b2c'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b2d.exa.snu',
         sha256='2cea2f2e084c4491fc1f216cb5bd1a08a0862e896743592e0e34851083481244',frames=102340,blocks=21,
         first='343fc3d863f88caad54208689f8138240be62ce2090424a69b8710117ecdad0a',directory='mono-dialogue-xma-chcb-b2d'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b2e.exa.snu',
         sha256='d4929854ee76f24108d6af6053f632a724d9bf2fa27568826fe87123445c47ff',frames=60416,blocks=12,
         first='f9b0c155528b96d462f0a9d0338c5402f90bbbba9f5cc967648ed9904524aef0',directory='mono-dialogue-xma-chcb-b2e'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0000b2f.exa.snu',
         sha256='d59f4126eb77cd517819fc1aa7655c5955fe73530925bdf3e1937b4f4d51c4ac',frames=77312,blocks=16,
         first='ba5c15d93a40c9393d986d36b2b8c93d112809138c54476235e6f1576c28e105',directory='mono-dialogue-xma-chcb-b2f'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd0.exa.snu',
         sha256='757d0730c9ba56b900c7388a8f7622a713ee03bc1529fe3882e48865f290409f',frames=104960,blocks=21,
         first='7465b825cd97f0c104e121c1494b288db494b941a7a5cae9a121bf3654a4e307',directory='mono-dialogue-xma-chcb-5bd0'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd1.exa.snu',
         sha256='71383e7f7736b0c7badee26ca2c6d30a03304678b8dc110a89c84c399634e6cf',frames=102144,blocks=21,
         first='ad6891382d1fbf2239379b8137a611d5912e63866a49188a6a02dd7f10bcdae9',directory='mono-dialogue-xma-chcb-5bd1'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd2.exa.snu',
         sha256='613ed1ff7a3d429111c4be4de526b2f5ecb36fc385394ba7b04f9cc6ac32e396',frames=96060,blocks=19,
         first='cb56b59b1f81514867731eb93f3662ad3b97c1334fba2c6195dffd15f8a24d0b',directory='mono-dialogue-xma-chcb-5bd2'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd3.exa.snu',
         sha256='a094eddb1e4f7dda1f5bfb85bcf8308f2ac72ee82ab98af60699826f5083d26c',frames=98560,blocks=20,
         first='6c1bb22d8ab5459d493ec58106f5cc86e96f3b8d34a42ea4337b409a07ebcfd1',directory='mono-dialogue-xma-chcb-5bd3'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd4.exa.snu',
         sha256='f3a7147dd822638426de2a1c7a10c58c01334e9b6e2472de7c451891cf5eae91',frames=76672,blocks=16,
         first='78c3233e0cb71d2c257a2e465348f6f09afa0ab70fc8a5a053bf1f346abec764',directory='mono-dialogue-xma-chcb-5bd4'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd5.exa.snu',
         sha256='465a3926e3e2c085bfe463d3dfe96413cd926488a272751691b99e995974f0b8',frames=104448,blocks=21,
         first='f9cb783ff65fff2a958b77422cab40b497f031dd8bbbe8b069abc061c810ae93',directory='mono-dialogue-xma-chcb-5bd5'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd6.exa.snu',
         sha256='fd42543135f3d0071a8e6e4ac6662cf8753f12f1bfc0673908641437b1a79d85',frames=58240,blocks=12,
         first='2cd69c5516bcc61a45902687c445d0ca579257cd4c9bf2f67ec30dc58fcf8eee',directory='mono-dialogue-xma-chcb-5bd6'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd7.exa.snu',
         sha256='0faabd6a5e81c8f28dc9e87e01a499e9d30e2372940c8c5de04b429ea301474b',frames=117760,blocks=24,
         first='0d142214dbad0259a9f9a3694bcfce38bc67c036b5fa36a4770661e1ba6f7b0b',directory='mono-dialogue-xma-chcb-5bd7'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd8.exa.snu',
         sha256='ac8afd3ee148948f6d32aadd59cbd461ffd5250d60971c61d578712d08c042fc',frames=79360,blocks=16,
         first='84f72f3598d61a5dc52081bba35a6d9af1982316e00eae24dd5200355b1b85c4',directory='mono-dialogue-xma-chcb-5bd8'),
    dict(source='Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_0005bd9.exa.snu',
         sha256='c6e8cc9d2dfb5a06f645f84e9191ed59ab1f84583955d6f7f2a854a6936c0d10',frames=64064,blocks=13,
         first='54e166d15372981b827dbef3d38fe2039f8623caf92a5bf4f1ed3e5ceb7a0466',directory='mono-dialogue-xma-chcb-5bd9'),
    # Reached by the recorded bridge replay at scene 705; the first reader claim
    # is the exact 1584-byte block captured in that run's audio rejection.
    dict(source='Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0000afe.exa.snu',
         sha256='52ec7e11a6d69abbab14a23939b005010a5cc8010439efacc3037b903332e981',frames=83035,blocks=17,
         first='b0f708cba2edecfad57d7ac8ad90d137ad3e0184e06deae5e8ed07b61a6d9b4c',directory='mono-dialogue-xma-homer-afe'),
)

# Homer bridge reaction selection varies between otherwise similar traversals.
# Each source is pinned to its own original file and first reader-owned block;
# the full decoder/stock comparison below qualifies every block independently.
_HOMER_AF_VARIANTS = (
    ('af0','1c37b7e8bed96f9d6b26fac806d73781a067d18a10b9b399087972505b2beb7d',135544,27,'8d31f263553f570d688163d9c87de86da78016694b29d437ffeed866c6c26b0d'),
    ('af1','287b59ae3ebb8e1de85f8d7cce75110d31ec8a152bad99896368bc5e6f27ff8b',87252,18,'f4d160ee81d55e56ff3846b737590fa243fbd52101414963cade036cf144f843'),
    ('af2','5deddafd5dcbf9c961ccdb8177973190273fed5b8adcd378777d161a7e153085',90471,18,'aafb43d6a765839c618a169da1aff009055227a30d11592a1549e7f9584f5a39'),
    ('af3','796b7e54b07700d2b9d68d04afef97d9b51ef07792a1aba9468d7ef78d932e8e',103367,21,'93bd49d71db6da78b618db920d0d196d73f2f8099936e739038e95b47755d716'),
    ('af4','06ecf030dd1cc60af95574d4f1fd5b6e7bac4b2250dda052014df05026609f2e',149657,30,'56b6da8b4e51c74d9499f9618f53cafc6ed683ed3ad8cab7a281fe12c0d204aa'),
    ('af5','afe61ffe8b421bdc142709af27cf4a175a5f6e75d6df4595cba53cc793ba3c80',73742,15,'c3a3880a80ed79d09492a836019c7bc525b0b027568ecbbaa872253534311b3d'),
    ('af6','08805b139ace40f5b47d3ca78592b8a0076cb3a713ae68f20d3b3db725af5069',40313,8,'7ae24bc6060f52d163149c6050e88712b0ed85afc148812399211da12c0ba59c'),
    ('af7','ab4f028be81594ad09f30cc2a29b75bcb78324389d4786a8c438dabb13ed9bc6',132416,26,'aee4a8bac25d1227abb0bd2199a857a2e3a7e6d12d924b2925b502908f2dcef5'),
    ('af8','8d579829e2566cf29f342e25a7834acad75901842d4f4e3f577e841c7cb53a70',92493,19,'41199a6c14965c57af5da204b3d237dcbc777fb2a3a99fffc1a0e798a231cfef'),
    ('af9','bbc7d7d48556841abb286c4e54f784fd3b7fb54b6a3ed8c2758438aa64e788f8',101911,20,'bdd6cbfd637cd2819ca08c2340179cf676b5a0cb47e7c61dad7a459385a0418d'),
    ('afa','48b996ce05068e4a88fa5c88bc41736a8e162f29757ed2a5f85991d249f1258a',81212,16,'67084e6e4010df537bb6cd6b31ffd646e4297956ceb00fa6330c97048bcf02d1'),
    ('afb','f9ccd4c769a385e05ad77b4c41fb91072b17f18513f5a8e9d1938cd89cde42cc',72910,15,'d26f464061237f804439477011d18b12d060a8a68cca8381748f0c6bf0bcacbb'),
    ('afc','15318f2db12d9c669bb9597e8b6f074ced65e50a25f0509970d4b12062557478',76728,16,'f9975c0abcf59b4e34cd94b00b39876f98ae4a4939ea688a96fd4114fb4519d3'),
    ('afd','b734cc988ab4be4db07ebf64bca808e560656029a83a959ab8140819d1165c39',85635,17,'ccf6a698320c62bc80649563ea34864805a9a7808d8b7a9b4ba36eb5fc449160'),
    ('aff','bb4d56e110e6c6189a38ee0ceffde4b8ab081e8f47fdb1a45cf19c990225ad18',63558,13,'717ef6a5d5855c83831ab5677b249b7b85714372eabfbdfe466ac6b14f361477'),
)
PROFILES += tuple(dict(
    source=f'Simpsons Game, The (USA)/audiostreams/mr_xxx_0/d_homr_xxx_0000{code}.exa.snu',
    sha256=digest, frames=frames, blocks=blocks, first=first,
    directory=f'mono-dialogue-xma-homer-{code}',
) for code,digest,frames,blocks,first in _HOMER_AF_VARIANTS)

# Chocolate rabbit reactions vary with route timing; 66eb is the exact
# 0300BB804001CB53 stream encountered at scene 683 of the bridge replay.
# 66fa is excluded: its 8-byte header aliases the already admitted 0b2b
# source, and header-only runtime lookup cannot safely distinguish their blocks.
_CHCB_66_VARIANTS = (
    ('66ea','9d5e3cc2687f985674245b59a4c1571e37c22a71fdb021b502ca504737630775',95744,19,'f8f3ade501c8a0a22a0ab184d1feef1fb057c56d7ca92a7a1139ef7ae883b779'),
    ('66eb','35fed73ce7fcbc5604d405c3ebc1d97617ca43c9830e3fe56117771a0a53bf2e',117587,24,'54fff9a14581b8827bee64bad53bbfbc0450b3765787a2735be9d6ac17f0b2d2'),
    ('66ec','7f4f1282ea5b2c496a93e0c18bc57edb72848848b5887cbde323dec1055de830',94976,19,'1bf6fae8de595d1bc7691a217c4910c8a477ad6d292a721e1fcf1d3fb2c317c2'),
    ('66ed','d991fbdf16afbb763ba13629a283d562b390486b032e8a23baff99109720beaa',91961,19,'b6490570b898013bb498cd0fc6be98f6af66704dc521da0b5304438e7b1e7849'),
    ('66ee','1ce5bb0ddecc26a6a6589a263452caa6fb8634c62b778576541150bc78641f12',112572,23,'2ebd684322d3b60b56df900d22fc8b990723f2a4639235726cec6b8d7388264c'),
    ('66ef','05793491c6a4f92e159595500b265aa8614a2fe6def08bbaeffccf4b417dd2d1',172032,34,'aaab4b2a1b1c964b60ef223bfc4d8747beb22ea32aec36ed9a24053e37db9030'),
    ('66f0','ed94797b629089f452bc273ce18c10c46ae4568960b2a9b8eb505fcf4679f00e',93184,19,'8748e5a7017ed9d0663512cb9f926bc1fa03e6f0a19aa8b5d0d90ff688271b10'),
    ('66f1','8f0141cb48cd8a72e3efa5acdd139dad153118dae24265173beb5c347c4c04d3',91904,19,'7cff3d1fe17d313e2e69b1275a61427b84d18f3978707970177cb40e5b14881d'),
    ('66f2','ddd799f7cb3fd260665433aa021550a2479ce2a3b2e158676593acad8d321e18',102912,21,'87257371144714ef77ec39cda631726bf67c3b83407d216c6028afb00d558daa'),
    ('66f3','7449f5e27610fd8049b8e742e54b0473ce06752bb79d8f8b408064a21ef15c97',87808,18,'06bbbdfc2a0105526030d71d992c0cd5fe21e8fd5b1541dccffcf87528996fca'),
    ('66f4','663b3e8c59c8dc87b58298e5150d7682b342482517b74cdbf9d2f5789e409f1c',137728,27,'31df9d3c5aea07d042552de03b03810380366f703168486da88632a993d889ad'),
    ('66f5','cc8f5a224629af765aa9b988bf6beaa737d5079c1d7aeafe970c05187b0177ab',69248,14,'bd7e779bd6f327413972d080ba24d558364ea1b5de715fa6ee1211e93c7b110b'),
    ('66f6','9b6e3ffb8da4dc167060ce7360adba26948b20588c98cecfab57da70fe272c2c',113152,23,'a17c3bf8cb45d13615ce2d540b342160e34f490e800cf3d2dfd8bb889b2d18cc'),
    ('66f7','5cf1654ee83fa81c76fc666a45f8f395470988f49f1e9f6fca1a820d57cbe8bb',88320,18,'48afd70bc7963593af235424ae401f0e95eb6e55ee19248f98965c495fe84d43'),
    ('66f8','45ea79bb0bce0f8fca54838a887db18ff37e2b48103aa67e7c6e1aecf23ef0be',121088,24,'268b7b3a54c0bd72681c37297cbbef76a5ba36e1877444b29b541341fcb425d5'),
    ('66f9','3e705af509b4512c10c75ed7157b09a30b6286b7bb453e8c714548d337956a88',70400,14,'20c4e0f0e750c570b5cb275b91fb6f3842f3f536952d92729cb63b9957c8a24c'),
    ('66fb','ac0d21983fd2495883fc0460bb8027f1199af8a213028f4f213de82aa1a8552a',89184,18,'ff39238532388f9fac4be2156d1a03ba1271fba2a09fea73fa08c190213b07b2'),
    ('66fc','fa3d4b329b51e98dab1333eed3a7bc9c59fe08e75cfcf0ed0efea859263fa5d8',131584,26,'8f670581cc7eb53612859fe2ccef172e9970554cf307956a902ef93edaa15c3c'),
    ('66fd','daa578ce7bc880a6a659680410e77c88376c730808376b7d5e5bb73dbcac8d8a',99584,20,'aca3e21d1b679526a51cad7934a9f81988374a6d7830a581165f58b945c774cb'),
    ('66fe','b12a5344663af13674346bc8d421828d2d8cf56b1a28df47b983662c9fda9cf3',54272,11,'4a8812ae3459c72219470e9678fadc170cc36bbc4d93b7bf6552168bb1a5148f'),
    ('66ff','a326ade9f0de455d47cf43ea7ec871769edbf4a7aa4ac5e216fe3462de9ec165',87296,18,'ec56d7f400fa9edc30f58116e60b9271c165b17d76e6d20c33cd43c41a24c903'),
)
PROFILES += tuple(dict(
    source=f'Simpsons Game, The (USA)/audiostreams/cb_xxx_0/d_chcb_xxx_000{code}.exa.snu',
    sha256=digest, frames=frames, blocks=blocks, first=first,
    directory=f'mono-dialogue-xma-chcb-{code}',
) for code,digest,frames,blocks,first in _CHCB_66_VARIANTS)

_toolchain = None

def decoder_toolchain(directory):
    """Verify and compile the identical decoder harness once for this batch."""
    global _toolchain
    if _toolchain is not None:
        return _toolchain
    OUT = directory
    q.OUT = OUT
    spec=importlib.util.spec_from_file_location('dialogue_toolchain',q.ROOT/'tests/test_host_fp.py')
    tc=importlib.util.module_from_spec(spec);spec.loader.exec_module(tc)
    compiler,env=tc.toolchain();env['PATH']=str(q.INSTALL/'bin')+os.pathsep+env['PATH']
    q.run([sys.executable,'-B',q.ROOT/'tools/build_native_audio_codec.py','--verify'],env,'codec-verify')
    source=OUT/'harness.cpp';source.write_text(q.CPP, encoding="utf-8")
    q.run([compiler,'/nologo','/std:c++20','/EHsc','/MD','/O2','/fp:strict','/DNOMINMAX','/DWIN32_LEAN_AND_MEAN',
        f'/I{q.ROOT}',f'/I{q.INSTALL/"include"}',source,q.ROOT/'audio/native_xma_codec.cpp',f'/Fo{OUT}{os.sep}',
        '/Fe'+str(OUT/'harness.exe'),'/link',q.INSTALL/'lib/avcodec-simpsonsxma.lib',q.INSTALL/'lib/avutil-simpsonsxma.lib','/INCREMENTAL:NO'],env,'compile')
    _toolchain = OUT/'harness.exe', env
    return _toolchain


def qualify(profile):
    SOURCE=profile['source'];DIGEST=profile['sha256'];OUT=q.ROOT/'build'/profile['directory']
    data=(q.ROOT/SOURCE).read_bytes();q.need(q.sha(data)==DIGEST,'Dialogue original changed')
    info=inspect_snu(data);h=info['header'];a=info['audio']
    q.need(h['channels']==1 and h['sample_rate']==48000 and not h['loop'] and h['samples']==profile['frames'],'Dialogue format changed')
    blocks=q.split_blocks(data,a['audio_offset'],a['audio_offset']+a['audio_size'],1)
    q.need(len(blocks)==profile['blocks'],'Dialogue block count changed')
    OUT.mkdir(parents=True,exist_ok=True);q.OUT=OUT
    harness,env=decoder_toolchain(OUT)
    packets=bytearray();ends=[]
    for b in blocks:
        raw=bytearray(data[b['offset']:b['offset']+b['bytes']]);raw[0]&=0x7f;b['owned_sha256']=q.sha(raw)
        layer=b['layers'][0];b['terminal_padding']=b['offset']+b['bytes']-layer['payload_offset']-layer['payload_bytes']
        packets+=data[layer['payload_offset']:layer['payload_offset']+layer['payload_bytes']]+b'\xff'*layer['restored_ff_bytes']
        ends.append(len(packets)//2048)
    q.need(blocks[0]['owned_sha256']==profile['first'],'Live reader claim differs')
    packet_path=OUT/'mono.packets';packet_path.write_bytes(packets);raw=None;runs=[];margins=[]
    for mode,split in (('xma1',0),('xma2',0),('xma2',1)):
        label=f'{mode}-{split}';pcm=OUT/(label+'.f32le');csv_path=OUT/(label+'.csv')
        result=q.run([harness,mode,1,packet_path,pcm,csv_path,split],env,label)
        lines=result.stdout.decode().splitlines();modules={}
        for line in lines:
            if line.startswith('module '):
                _,name,path=line.split(' ',2);q.need(Path(path).resolve()==(q.INSTALL/'bin'/name).resolve(),'Unowned decoder DLL');modules[name]=path
        summary=next(x for x in lines if x.startswith('result ')).split()
        q.need(len(modules)==3 and summary[-1]=='no_eof_sent' and int(summary[1])==len(packets)//2048,'Decoder provenance/accounting changed')
        decoded=pcm.read_bytes();q.need(len(decoded)==int(summary[2])*4,'Mono PCM extent differs')
        if raw is None:raw=decoded
        q.need(raw==decoded,'Raw codec variants/read schedules differ')
        runs.append(dict(mode=mode,split=split,frames=len(raw)//4,sha256=q.sha(raw),modules=modules))
        if mode=='xma2' and split==0:
            produced={}
            with csv_path.open(newline="", encoding="utf-8") as stream:
                reader=csv.reader(stream)
                next(reader, None)
                for row in reader:
                    _,accepted,offset,count=map(int,row[:4]);produced[accepted]=offset+count
            quota=384
            for b,end in zip(blocks,ends):
                quota+=b['samples'];margin=max((v for k,v in produced.items() if k<=end),default=0)-quota
                q.need(margin>=0,'Mono block quota requires future input');margins.append(margin)
    wave=OUT/'diagnostic.wav';wave.write_bytes(q.riff(packets,1))
    stock=q.run([q.CLI,'-nostdin','-hide_banner','-loglevel','error','-xerror','-threads',1,'-f','wav','-i',wave,
                 '-map','0:a:0','-c:a','pcm_f32le','-f','f32le','pipe:1'],env,'stock')
    native_floats=array.array('f');native_floats.frombytes(raw);stock_floats=array.array('f');stock_floats.frombytes(stock.stdout)
    usable=min(len(stock_floats),len(native_floats)-576)-512;q.need(usable>1024,'Independent decoder output too short')
    error=max(abs(native_floats[i+576]-stock_floats[i]) for i in range(usable));q.need(error<.0001,'Independent mono decoder differs')
    trimmed=q.sha(raw[384*4:(384+h['samples'])*4])
    report=dict(source=SOURCE,source_sha256=DIGEST,stream=info,header=data[16:24].hex(),blocks=blocks,runs=runs,
                raw_eof_sent=False,initial_skip_frames=384,minimum_per_block_surplus=min(margins),maximum_per_block_surplus=max(margins),
                stock_measured_offset_frames=576,stock_maximum_error=error,trimmed_sha256=trimmed)
    (OUT/'report.json').write_text(json.dumps(report,indent=2)+'\n', encoding="utf-8")
    rows=[]
    for b in blocks:
        l=b['layers'][0]
        rows.append('    {"%s",%d,%d,{%d,0,0},{%d,0,0},%d}' %
                    (b['owned_sha256'],b['bytes'],b['samples'],l['payload_bytes'],l['restored_ff_bytes'],b['terminal_padding']))
    header=','.join(f'0x{x:02X}' for x in data[16:24])
    q.need(q.sha((q.ROOT/SOURCE).read_bytes())==DIGEST,'Original changed during qualification')
    print(json.dumps({k:report[k] for k in ('source','header','minimum_per_block_surplus','stock_maximum_error','trimmed_sha256')}))
    return rows,header,trimmed

def main():
    results=[qualify(profile) for profile in PROFILES]
    text='// Independently qualified by tools/qualify_mono_dialogue_xma.py.\n#pragma once\n#include "all_menu_xma_certificates.h"\nnamespace Simpsons::Audio {\n'
    entries=[]
    for i,(profile,(rows,header,trimmed)) in enumerate(zip(PROFILES,results)):
        name='monoDialogueBlocks'+(str(i) if i else '')
        text+='inline constexpr std::array<EaXmaCertificate,'+str(len(rows))+'> '+name+' = {{\n'+',\n'.join(rows)+'\n}};\n'
        entries.append('    {{'+header+'},'+str(profile['frames'])+','+name+',{ "'+trimmed+'","",""}}')
    text+='inline constexpr std::array<MenuXmaProfile,'+str(len(entries))+'> monoDialogueProfiles = {{\n'+',\n'.join(entries)+'\n}};\n}\n'
    target=q.ROOT/'audio/mono_dialogue_xma_certificates.h'
    tmp=target.with_suffix('.tmp')
    tmp.write_text(text, encoding="utf-8")
    os.replace(tmp,target)

if __name__=='__main__':main()
