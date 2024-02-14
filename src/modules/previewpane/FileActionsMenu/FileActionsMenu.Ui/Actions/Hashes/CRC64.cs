﻿// Copyright (c) Microsoft Corporation
// The Microsoft Corporation licenses this file to you under the MIT license.
// See the LICENSE file in the project root for more information.

using System;
using System.Threading.Tasks;
using System.Windows;
using FileActionsMenu.Interfaces;
using Wpf.Ui.Controls;

namespace FileActionsMenu.Ui.Actions.Hashes
{
    internal sealed class CRC64(Hashes.Hashes.HashCallingAction hashCallingAction) : IAction
    {
        private readonly Hashes.Hashes.HashCallingAction _hashCallingAction = hashCallingAction;

        public string[] SelectedItems { get => []; set => _ = value; }

        public string Header => "CRC64";

        public IAction.ItemType Type => IAction.ItemType.HasSubMenu;

        public int Category => 0;

        public IconElement? Icon => null;

        public bool IsVisible => true;

        public IAction[]? SubMenuItems =>
        [
            new CRC64Hex(_hashCallingAction),
            new CRC64Decimal(_hashCallingAction),
        ];

        public Task Execute(object sender, RoutedEventArgs e)
        {
            throw new InvalidOperationException();
        }
    }
}