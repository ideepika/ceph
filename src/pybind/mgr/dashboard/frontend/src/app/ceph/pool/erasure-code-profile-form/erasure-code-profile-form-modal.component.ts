import { Component, EventEmitter, OnInit, Output } from '@angular/core';
import { Validators } from '@angular/forms';

import { I18n } from '@ngx-translate/i18n-polyfill';
import { BsModalRef } from 'ngx-bootstrap/modal';

import { ErasureCodeProfileService } from '../../../shared/api/erasure-code-profile.service';
import { CrushNodeSelectionClass } from '../../../shared/classes/crush.node.selection.class';
import { ActionLabelsI18n } from '../../../shared/constants/app.constants';
import { CdFormBuilder } from '../../../shared/forms/cd-form-builder';
import { CdFormGroup } from '../../../shared/forms/cd-form-group';
import { CdValidators } from '../../../shared/forms/cd-validators';
import { CrushNode } from '../../../shared/models/crush-node';
import { ErasureCodeProfile } from '../../../shared/models/erasure-code-profile';
import { FinishedTask } from '../../../shared/models/finished-task';
import { TaskWrapperService } from '../../../shared/services/task-wrapper.service';

@Component({
  selector: 'cd-erasure-code-profile-form-modal',
  templateUrl: './erasure-code-profile-form-modal.component.html',
  styleUrls: ['./erasure-code-profile-form-modal.component.scss']
})
export class ErasureCodeProfileFormModalComponent extends CrushNodeSelectionClass
  implements OnInit {
  @Output()
  submitAction = new EventEmitter();

  tooltips = this.ecpService.formTooltips;
  PLUGIN = {
    LRC: 'lrc', // Locally Repairable Erasure Code
    SHEC: 'shec', // Shingled Erasure Code
    JERASURE: 'jerasure', // default
    ISA: 'isa' // Intel Storage Acceleration
  };
  plugin = this.PLUGIN.JERASURE;

  form: CdFormGroup;
  plugins: string[];
  names: string[];
  techniques: string[];
  action: string;
  resource: string;

  constructor(
    private formBuilder: CdFormBuilder,
    public bsModalRef: BsModalRef,
    private taskWrapper: TaskWrapperService,
    private ecpService: ErasureCodeProfileService,
    private i18n: I18n,
    public actionLabels: ActionLabelsI18n
  ) {
    super();
    this.action = this.actionLabels.CREATE;
    this.resource = this.i18n('EC Profile');
    this.createForm();
    this.setJerasureDefaults();
  }

  createForm() {
    this.form = this.formBuilder.group({
      name: [
        null,
        [
          Validators.required,
          Validators.pattern('[A-Za-z0-9_-]+'),
          CdValidators.custom(
            'uniqueName',
            (value: string) => this.names && this.names.indexOf(value) !== -1
          )
        ]
      ],
      plugin: [this.PLUGIN.JERASURE, [Validators.required]],
      k: [1], // Will be replaced by plugin defaults
      m: [1], // Will be replaced by plugin defaults
      crushFailureDomain: ['host'],
      crushRoot: ['default'], // default for all - is a list possible???
      crushDeviceClass: [''], // set none to empty at submit - get list from configs?
      directory: [''],
      // Only for 'jerasure' and 'isa' use
      technique: ['reed_sol_van'],
      // Only for 'jerasure' use
      packetSize: [2048, [Validators.min(1)]],
      // Only for 'lrc' use
      l: [1, [Validators.required, Validators.min(1)]],
      crushLocality: [''], // set to none at the end (same list as for failure domains)
      // Only for 'shec' use
      c: [1, [Validators.required, Validators.min(1)]]
    });
    this.form.get('plugin').valueChanges.subscribe((plugin) => this.onPluginChange(plugin));
  }

  onPluginChange(plugin: string) {
    this.plugin = plugin;
    if (plugin === this.PLUGIN.JERASURE) {
      this.setJerasureDefaults();
    } else if (plugin === this.PLUGIN.LRC) {
      this.setLrcDefaults();
    } else if (plugin === this.PLUGIN.ISA) {
      this.setIsaDefaults();
    } else if (plugin === this.PLUGIN.SHEC) {
      this.setShecDefaults();
    }
  }

  private setNumberValidators(name: string, required: boolean) {
    const validators = [Validators.min(1)];
    if (required) {
      validators.push(Validators.required);
    }
    this.form.get(name).setValidators(validators);
  }

  private setKMValidators(required: boolean) {
    ['k', 'm'].forEach((name) => this.setNumberValidators(name, required));
  }

  private setJerasureDefaults() {
    this.requiredControls = ['k', 'm'];
    this.setDefaults({
      k: 4,
      m: 2
    });
    this.setKMValidators(true);
    this.techniques = [
      'reed_sol_van',
      'reed_sol_r6_op',
      'cauchy_orig',
      'cauchy_good',
      'liberation',
      'blaum_roth',
      'liber8tion'
    ];
  }

  private setLrcDefaults() {
    this.requiredControls = ['k', 'm', 'l'];
    this.setKMValidators(true);
    this.setNumberValidators('l', true);
    this.setDefaults({
      k: 4,
      m: 2,
      l: 3
    });
  }

  private setIsaDefaults() {
    this.requiredControls = [];
    this.setKMValidators(false);
    this.setDefaults({
      k: 7,
      m: 3
    });
    this.techniques = ['reed_sol_van', 'cauchy'];
  }

  private setShecDefaults() {
    this.requiredControls = [];
    this.setKMValidators(false);
    this.setDefaults({
      k: 4,
      m: 3,
      c: 2
    });
  }

  private setDefaults(defaults: object) {
    Object.keys(defaults).forEach((controlName) => {
      if (this.form.get(controlName).pristine) {
        this.form.silentSet(controlName, defaults[controlName]);
      }
    });
  }

  ngOnInit() {
    this.ecpService
      .getInfo()
      .subscribe(
        ({
          plugins,
          names,
          directory,
          nodes
        }: {
          plugins: string[];
          names: string[];
          directory: string;
          nodes: CrushNode[];
        }) => {
          this.initCrushNodeSelection(
            nodes,
            this.form.get('crushRoot'),
            this.form.get('crushFailureDomain'),
            this.form.get('crushDeviceClass')
          );
          this.plugins = plugins;
          this.names = names;
          this.form.silentSet('directory', directory);
        }
      );
  }

  onSubmit() {
    if (this.form.invalid) {
      this.form.setErrors({ cdSubmitButton: true });
      return;
    }
    const profile = this.createJson();
    this.taskWrapper
      .wrapTaskAroundCall({
        task: new FinishedTask('ecp/create', { name: profile.name }),
        call: this.ecpService.create(profile)
      })
      .subscribe(
        undefined,
        () => {
          this.form.setErrors({ cdSubmitButton: true });
        },
        () => {
          this.bsModalRef.hide();
          this.submitAction.emit(profile);
        }
      );
  }

  private createJson() {
    const pluginControls = {
      technique: [this.PLUGIN.ISA, this.PLUGIN.JERASURE],
      packetSize: [this.PLUGIN.JERASURE],
      l: [this.PLUGIN.LRC],
      crushLocality: [this.PLUGIN.LRC],
      c: [this.PLUGIN.SHEC]
    };
    const ecp = new ErasureCodeProfile();
    const plugin = this.form.getValue('plugin');
    Object.keys(this.form.controls)
      .filter((name) => {
        const pluginControl = pluginControls[name];
        const value = this.form.getValue(name);
        const usable = (pluginControl && pluginControl.includes(plugin)) || !pluginControl;
        return usable && value && value !== '';
      })
      .forEach((name) => {
        this.extendJson(name, ecp);
      });
    return ecp;
  }

  private extendJson(name: string, ecp: ErasureCodeProfile) {
    const differentApiAttributes = {
      crushFailureDomain: 'crush-failure-domain',
      crushRoot: 'crush-root',
      crushDeviceClass: 'crush-device-class',
      packetSize: 'packetsize',
      crushLocality: 'crush-locality'
    };
    const value = this.form.getValue(name);
    ecp[differentApiAttributes[name] || name] = name === 'crushRoot' ? value.name : value;
  }
}
