module.exports = [
  {
    type: 'heading',
    defaultValue: 'Jeep Trails'
  },
  {
    type: 'text',
    defaultValue: 'Daily trail scenes, time-of-day skies, and an hourly Jeep drive.'
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Display'
      },
      {
        type: 'color',
        messageKey: 'TextColor',
        defaultValue: '0xFFFFFF',
        label: 'Time / Date Color',
        sunlight: true
      },
      {
        type: 'toggle',
        messageKey: 'ShowDate',
        label: 'Show Date',
        defaultValue: true
      },
      {
        type: 'toggle',
        messageKey: 'UseBoldTime',
        label: 'Bold Time Font',
        defaultValue: true
      }
    ]
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Trail Scene'
      },
      {
        type: 'select',
        messageKey: 'SceneMode',
        label: 'Scene Mode',
        defaultValue: '0',
        options: [
          { label: 'Changes each day', value: '0' },
          { label: 'Fixed scene', value: '1' }
        ]
      },
      {
        type: 'select',
        messageKey: 'FixedScene',
        label: 'Fixed Scene',
        defaultValue: '0',
        options: [
          { label: 'Desert Canyon', value: '0' },
          { label: 'Forest Trail', value: '1' },
          { label: 'Mountain Pass', value: '2' },
          { label: 'Beach Dunes', value: '3' },
          { label: 'Snowy Alpine', value: '4' },
          { label: 'Farm Country', value: '5' },
          { label: 'Moab Slickrock', value: '6' }
        ]
      }
    ]
  },
  {
    type: 'section',
    items: [
      {
        type: 'heading',
        defaultValue: 'Jeep Drive'
      },
      {
        type: 'toggle',
        messageKey: 'AnimationsEnabled',
        label: 'Hourly Drive Animation',
        defaultValue: true
      },
      {
        type: 'slider',
        messageKey: 'DriveMinute',
        label: 'Drive at minute',
        defaultValue: 0,
        min: 0,
        max: 59,
        step: 1
      },
      {
        type: 'toggle',
        messageKey: 'VibrateOnDrive',
        label: 'Vibrate when Jeep starts',
        defaultValue: false
      }
    ]
  },
  {
    type: 'submit',
    defaultValue: 'Save'
  }
];
